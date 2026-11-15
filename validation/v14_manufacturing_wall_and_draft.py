# Manufacturing analysis against exact geometry: the thickness of a wall and the draft of a wall.
#
# THE PROBLEM.  `manufacturing_analysis` reads a part's own field and says where it breaks a rule (see docs/manufacturing.md).  Two rules have an answer that follows from the part's dimensions alone:
#   * min_wall = 0.8: a free plate t mm thick is a wall of exactly t mm.  The check counts a wall as broken when it is more than 5 % short of the limit (what a field can say of a size is not exact), so a plate
#     is marked when t < 0.8 x 0.95 = 0.76 mm, and how much it is short is  0.8 - t - 0.04 mm.  Six plates, 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick.
#   * draft = 1.0 (pull direction z): a wall that leans a degrees from the pull direction has a draft of a degrees, whichever way it leans.  It is marked when a < 1 x 0.95 = 0.95 degrees, and it is
#     short by  1 - a - 0.05  degrees.  Walls leaning 0, 0.5, 0.96, 1.2 and 3 degrees.
# The exact answers are derived here from the geometry (the same cases as tests/internal/mfg_a1_staircase_wall.py and mfg_a4_drafted_walls.py, which have the pictures).
#
# WHAT THE SCRIPT DOES: builds the plates and the walls by hand from boxes, asks the check for each of them (eleven analyses, each a row of the model tree: `plate_1` ... `plate_6`, `wall_1` ... `wall_5`),
# reads the check's own field (the colour the window shows) at a point of each face, and stops with an AssertionError when a plate or wall that should be marked is not, one that should not is, or the
# shortfall is more than 0.03 mm (0.05 degrees) from the exact one.  A few seconds.  THIS IS A CHECK OF THE PROGRAM, NOT OF YOUR PART: "Not a substitute for the manufacturer's own check", as the report says.
# WHAT YOU SEE when you open it: the six plates and the five walls, coloured by the check where they break their rule (the plates stand 20 mm in front of the walls along y so that both can be seen at once), at the end
# of the file.  Each plate and each wall is its own analysis because the resolution of the view is relative to the size of each model: a plate 6 mm across is drawn with 0.1 mm samples, which a plate 0.3 mm thick needs.
import math

from fieldes import *
from fieldes.stdlib.fields import evaluate

view.set_bounds((-10, -30, -2), (60, 15, 24), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of each model per unit (docs/caching-and-performance.md): a plate is 6 mm across, so a tenth of a millimetre per sample, and a wall 20 mm high 0.2 mm
view.set_quality(8)

failures = []
YP = -20.0                             # mm: the plates stand this far along y from the walls (in front of them from the usual camera, so that both can be seen)
THICKNESS = (0.3, 0.7, 0.74, 0.78, 0.85, 2.0)
LEAN = (0.0, 0.5, 0.96, 1.2, 3.0)


def margins(report, rule, points):
    ''' The check's own field for `rule` at points: above 0 where the rule is broken there, by how much in the unit of the rule '''
    return evaluate(report.fields[rule], points)


# --- walls: six plates 6 x 6 mm, 9 mm apart along x, of the thicknesses 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm, each checked against a minimum wall of 0.8 mm
plate_1 = box((6, 6, 0.3), position=(3, YP + 3, 0.15))
plate_report_1 = manufacturing_analysis(plate_1, constraints=[manufacturing_constraint(min_wall=0.8)], samples=1500)
plate_2 = box((6, 6, 0.7), position=(12, YP + 3, 0.35))
plate_report_2 = manufacturing_analysis(plate_2, constraints=[manufacturing_constraint(min_wall=0.8)], samples=1500)
plate_3 = box((6, 6, 0.74), position=(21, YP + 3, 0.37))
plate_report_3 = manufacturing_analysis(plate_3, constraints=[manufacturing_constraint(min_wall=0.8)], samples=1500)
plate_4 = box((6, 6, 0.78), position=(30, YP + 3, 0.39))
plate_report_4 = manufacturing_analysis(plate_4, constraints=[manufacturing_constraint(min_wall=0.8)], samples=1500)
plate_5 = box((6, 6, 0.85), position=(39, YP + 3, 0.425))
plate_report_5 = manufacturing_analysis(plate_5, constraints=[manufacturing_constraint(min_wall=0.8)], samples=1500)
plate_6 = box((6, 6, 2.0), position=(48, YP + 3, 1.0))
plate_report_6 = manufacturing_analysis(plate_6, constraints=[manufacturing_constraint(min_wall=0.8)], samples=1500)

# --- draft: five walls 6 x 10 x 20 mm, 14 mm apart along x, leaning 0, 0.5, 0.96, 1.2 and 3 degrees off the pull direction z (turned about the line through their foot), each checked against a draft of 1 degree
wall_1 = rotate_y(box((6, 10, 20), position=(0, 5, 10)), math.radians(0.0), center=(0, 5, 0))
wall_report_1 = manufacturing_analysis(wall_1, constraints=[manufacturing_constraint(draft=1.0, pull_direction=(0, 0, 1))], samples=2000)
wall_2 = rotate_y(box((6, 10, 20), position=(14, 5, 10)), math.radians(0.5), center=(14, 5, 0))
wall_report_2 = manufacturing_analysis(wall_2, constraints=[manufacturing_constraint(draft=1.0, pull_direction=(0, 0, 1))], samples=2000)
wall_3 = rotate_y(box((6, 10, 20), position=(28, 5, 10)), math.radians(0.96), center=(28, 5, 0))
wall_report_3 = manufacturing_analysis(wall_3, constraints=[manufacturing_constraint(draft=1.0, pull_direction=(0, 0, 1))], samples=2000)
wall_4 = rotate_y(box((6, 10, 20), position=(42, 5, 10)), math.radians(1.2), center=(42, 5, 0))
wall_report_4 = manufacturing_analysis(wall_4, constraints=[manufacturing_constraint(draft=1.0, pull_direction=(0, 0, 1))], samples=2000)
wall_5 = rotate_y(box((6, 10, 20), position=(56, 5, 10)), math.radians(3.0), center=(56, 5, 0))
wall_report_5 = manufacturing_analysis(wall_5, constraints=[manufacturing_constraint(draft=1.0, pull_direction=(0, 0, 1))], samples=2000)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the eleven analyses, coloured by the check where they break their rule (the eye of each row in the model tree shows or hides it)
plate_report_1                         # the plate 0.30 mm thick against min_wall = 0.8: marked
plate_report_2                         # 0.70 mm: marked
plate_report_3                         # 0.74 mm: marked
plate_report_4                         # 0.78 mm: kept (the check counts a wall as broken from 5 % under the limit)
plate_report_5                         # 0.85 mm: kept
plate_report_6                         # 2.00 mm: kept
wall_report_1                          # the wall leaning 0 degrees against draft = 1: marked
wall_report_2                          # 0.5 degrees: marked
wall_report_3                          # 0.96 degrees: kept (the check counts a wall as broken from 5 % under the limit)
wall_report_4                          # 1.2 degrees: kept
wall_report_5                          # 3 degrees: kept


def check_plate(k, t, report):
    ''' the plate k (the k-th from x = 0, thickness t mm): the check's margin at the middle of its top and of its bottom face against the exact answer '''
    top, bottom = margins(report, 'min_wall', [(9 * k + 3, YP + 3, t), (9 * k + 3, YP + 3, 0.0)])
    should = t < 0.76
    exact = 0.8 - t - 0.04
    print('  plate %.2f mm: top %+.3f, bottom %+.3f; exact: %s' % (t, top, bottom, ('marked, short by %.3f mm' % exact) if should else 'not marked'))
    if should:
        if not (top > 0 and bottom > 0):
            failures.append('the plate %.2f mm is not marked' % t)
        elif abs(top - exact) > 0.03:
            failures.append('the plate %.2f mm: short by %.3f, exact %.3f' % (t, top, exact))
    elif top > 0 or bottom > 0:
        failures.append('the plate %.2f mm is marked and is not too thin' % t)


print('walls: the plates 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick against a wall rule of 0.8 mm (marked when under 0.76)')
check_plate(0, THICKNESS[0], plate_report_1)
check_plate(1, THICKNESS[1], plate_report_2)
check_plate(2, THICKNESS[2], plate_report_3)
check_plate(3, THICKNESS[3], plate_report_4)
check_plate(4, THICKNESS[4], plate_report_5)
check_plate(5, THICKNESS[5], plate_report_6)


def check_wall(k, a, wall, report):
    ''' the wall k (the k-th from x = 0, leaning a degrees): the check's margin on its two side faces at half height against the exact answer '''
    cx = 14.0 * k
    # a point on each side face at half height: where the wall's field changes sign along x at z = 10 (the wall leans, so its sides are not at cx -+ 3)
    xs = [cx - 6.0 + 12.0 * i / 6000.0 for i in range(6001)]
    inside = [v <= 0 for v in evaluate(wall, [(x, 5, 10.0) for x in xs])]
    crossings = [0.5 * (xs[i] + xs[i + 1]) for i in range(6000) if inside[i] != inside[i + 1]]
    if len(crossings) != 2:
        failures.append('the wall leaning %.2f degrees has %d faces at half height' % (a, len(crossings)))
        return
    left, right = margins(report, 'draft', [(crossings[0], 5, 10.0), (crossings[1], 5, 10.0)])
    should = a < 0.95
    exact = 1.0 - a - 0.05
    print('  wall leaning %.2f degrees: right %+.3f, left %+.3f; exact: %s' % (a, right, left, ('marked, short by %.3f degrees' % exact) if should else 'not marked'))
    if should:
        if not (right > 0 and left > 0):
            failures.append('the wall leaning %.2f degrees is not marked' % a)
        elif abs(right - exact) > 0.05 or abs(left - exact) > 0.05:
            failures.append('the wall leaning %.2f degrees: short by %.3f / %.3f degrees, exact %.3f' % (a, right, left, exact))
    elif right > 0 or left > 0:
        failures.append('the wall leaning %.2f degrees is marked and has enough draft' % a)


print('draft: walls leaning 0, 0.5, 0.96, 1.2 and 3 degrees off the pull direction against a draft of 1 degree (marked when under 0.95)')
check_wall(0, LEAN[0], wall_1, wall_report_1)
check_wall(1, LEAN[1], wall_2, wall_report_2)
check_wall(2, LEAN[2], wall_3, wall_report_3)
check_wall(3, LEAN[3], wall_4, wall_report_4)
check_wall(4, LEAN[4], wall_5, wall_report_5)

if failures:
    raise AssertionError('; '.join(failures))
print('OK')
