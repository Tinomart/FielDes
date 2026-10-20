# One part with a plate of 0.3 and one of 0.7 mm, a plate of 2 mm and a slot of 0.2 mm, held to a wall rule of 0.8 and a gap rule of 0.4: as it is,
# moved off the grid, turned 37 degrees, mirrored, and at half and twice the size with the rules scaled with it: the same four places must be marked
# and the same amounts short.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                      - gaps: _remedy_gaps (a narrow gap is closed, or made as wide as asked)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('a10_same_part_other_poses: one part with a plate of 0.3 and one of 0.7 mm, a plate of 2 mm and a slot of 0.2 mm, held to a wall rule of 0.8 and a gap rule of 0.4: as it is, moved off the grid, turned 37 degrees, mirrored, and at half and twice the size with the rules scaled with it: the same four places must be marked and the same amounts short')

def make():
    a = box((0, 0, 0), (6, 6, 0.3))
    b = box((9, 0, 0), (15, 6, 0.7))
    c = box((18, 0, 0), (24, 6, 2.0))
    d = difference(box((27, 0, 0), (36, 6, 4)), box((31, -1, 1), (31.2, 7, 5)))
    return union(union(union(a, b), c), d)
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
probes = [(3.0, 3.0, 0.3), (12.0, 3.0, 0.7), (21.0, 3.0, 2.0), (31.0, 3.0, 2.5)]        # (a face of each: the 0.3 plate, the 0.7 plate, the 2 mm plate, a wall of the slot)
rad = math.radians(37.0)
cx, cy = 18.0, 3.0

def turned(p):
    return (cx + (p[0] - cx) * math.cos(rad) - (p[1] - cy) * math.sin(rad), cy + (p[0] - cx) * math.sin(rad) + (p[1] - cy) * math.cos(rad), p[2])
poses = [('as it is', lambda s: s, lambda p: p, 1.0),
         ('moved off the grid by (0.457, -0.31, 0.113)', lambda s: move(s, (0.457, -0.31, 0.113)), lambda p: (p[0] + 0.457, p[1] - 0.31, p[2] + 0.113), 1.0),
         ('turned 37 degrees about z', lambda s: rotate_z(s, rad, center=(cx, cy, 0)), turned, 1.0),
         ('mirrored in x about x = 18', lambda s: reflect_x(s, 18.0), lambda p: (36.0 - p[0], p[1], p[2]), 1.0),
         ('half the size', lambda s: scale_xyz(s, (0.5, 0.5, 0.5)), lambda p: (0.5 * p[0], 0.5 * p[1], 0.5 * p[2]), 0.5),
         ('twice the size', lambda s: scale_xyz(s, (2.0, 2.0, 2.0)), lambda p: (2.0 * p[0], 2.0 * p[1], 2.0 * p[2]), 2.0)]
lo, hi = (-6, -6, -3), (80, 12, 10)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=8)
expected = [True, True, False, True]
short = [0.8 - 0.3 - 0.04, 0.8 - 0.7 - 0.04, None, 0.4 - 0.2 - 0.02]
rule_of = lambda s: [manufacturing_constraint(min_wall=0.8 * s, min_gap=0.4 * s)]
last = None
for name, shape_fn, point_fn, s in poses:
    posed = shape_fn(make())
    report = manufacturing_analysis(posed, constraints=rule_of(s), samples=8000)
    last = (report, posed)
    rules = ['min_wall', 'min_wall', 'min_wall', 'min_gap']
    got = []
    for p, rule_name in zip(probes, rules):
        q = point_fn(p)
        got.append(margin_at(report, rule_name, [q])[0] / s)                                   # (the amount short, brought back to the size of the part as it is)
    flagged = [g > 0 for g in got]
    t.check('%s: the same places are marked (the 0.3 plate, the 0.7 plate, not the 2 mm plate, the slot)' % name, flagged == expected, ['%.3f' % g for g in got], expected)
    for g, e, w in zip(got, expected, short):
        if e:
            t.near('%s: how much it is short (%.3f by hand, brought back to the first size)' % (name, w), g, w, 0.04, ' mm')
    n_regions = sum(len(report.report[r]['regions']) for r in ('min_wall', 'min_gap'))
    t.check('%s: three regions are listed (two plates and the slot)' % name, n_regions == 3, n_regions, 3)

t.summary()
result = last[0]
result
