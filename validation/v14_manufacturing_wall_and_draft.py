# Manufacturing analysis against exact geometry: the thickness of a wall and the draft of a wall.
#
# THE PROBLEM.  `manufacturing_analysis` reads a part's own field and says where it breaks a rule (see docs/manufacturing.md).  Two rules have an answer that follows from the part's dimensions alone:
#   * min_wall = 0.8: a free plate t mm thick is a wall of exactly t mm.  The check counts a wall as broken when it is more than 5 % short of the limit (what a field can say of a size is not exact), so a plate
#     is marked when t < 0.8 x 0.95 = 0.76 mm, and how much it is short is  0.8 - t - 0.04 mm.  Six plates, 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick.
#   * draft = 1.0 (pull direction z): a wall that leans a degrees from the pull direction has a draft of a degrees, whichever way it leans.  It is marked when a < 1 x 0.95 = 0.95 degrees, and it is
#     short by  1 - a - 0.05  degrees.  Walls leaning 0, 0.5, 0.96, 1.2 and 3 degrees.
# The exact answers are derived here from the geometry (the same cases as tests/internal/mfg_a1_staircase_wall.py and mfg_a4_drafted_walls.py, which have the pictures).
#
# WHAT THE SCRIPT DOES: builds the plates and the walls by hand from boxes, asks the check, reads the check's own field (the colour the window shows) at a point of each face, and stops with an AssertionError
# when a plate or wall that should be marked is not, one that should not is, or the shortfall is more than 0.03 mm (0.05 degrees) from the exact one.  A few seconds.  THIS IS A CHECK OF THE PROGRAM, NOT OF YOUR PART:
# "Not a substitute for the manufacturer's own check", as the report says.
import math

from fieldes import *
from fieldes.stdlib.fields import evaluate

failures = []


def margins(report, rule, points):
    ''' The check's own field for `rule` at points: above 0 where the rule is broken there, by how much in the unit of the rule '''
    return evaluate(report.fields[rule], points)


# --- walls
thickness = [0.3, 0.7, 0.74, 0.78, 0.85, 2.0]
part = None
for k, t in enumerate(thickness):
    plate = box((6, 6, t), position=(9 * k + 3, 3, t / 2))
    part = plate if part is None else union(part, plate)
report = manufacturing_analysis(part, constraints=[manufacturing_constraint(min_wall=0.8)], samples=8000)
print('walls: the plates 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick against a wall rule of 0.8 mm (marked when under 0.76)')
for k, t in enumerate(thickness):
    top, bottom = margins(report, 'min_wall', [(9 * k + 3, 3, t), (9 * k + 3, 3, 0.0)])
    should = t < 0.76
    exact = 0.8 - t - 0.04
    line = '  plate %.2f mm: top %+.3f, bottom %+.3f; exact: %s' % (t, top, bottom, ('marked, short by %.3f mm' % exact) if should else 'not marked')
    print(line)
    if should:
        if not (top > 0 and bottom > 0):
            failures.append('the plate %.2f mm is not marked' % t)
        elif abs(top - exact) > 0.03:
            failures.append('the plate %.2f mm: short by %.3f, exact %.3f' % (t, top, exact))
    elif top > 0 or bottom > 0:
        failures.append('the plate %.2f mm is marked and is not too thin' % t)

# --- draft
leans = [0.0, 0.5, 0.96, 1.2, 3.0]
part = None
for k, a in enumerate(leans):
    cx = 14.0 * k
    wall = rotate_y(box((6, 10, 20), position=(cx, 5, 10)), math.radians(a), center=(cx, 5, 0))
    part = wall if part is None else union(part, wall)
report = manufacturing_analysis(part, constraints=[manufacturing_constraint(draft=1.0, pull_direction=(0, 0, 1))], samples=9000)
print('draft: walls leaning 0, 0.5, 0.96, 1.2 and 3 degrees off the pull direction against a draft of 1 degree (marked when under 0.95)')
for k, a in enumerate(leans):
    cx = 14.0 * k
    # a point on each side face at half height: where the part's field changes sign along x at z = 10 (the wall leans, so its sides are not at cx -+ 3)
    xs = [cx - 6.0 + 12.0 * i / 6000.0 for i in range(6001)]
    inside = [v <= 0 for v in evaluate(part, [(x, 5, 10.0) for x in xs])]
    crossings = [0.5 * (xs[i] + xs[i + 1]) for i in range(6000) if inside[i] != inside[i + 1]]
    if len(crossings) != 2:
        failures.append('the wall leaning %.2f degrees has %d faces at half height' % (a, len(crossings)))
        continue
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

if failures:
    raise AssertionError('; '.join(failures))
print('OK')
