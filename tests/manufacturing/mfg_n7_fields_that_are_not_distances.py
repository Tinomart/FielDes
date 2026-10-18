# A fin of 0.6 mm and a slot of 0.2 mm (a small scene, 12 cells to the millimetre) whose field is not a distance -- the field of the part times 2 (a
# slope of 2), times 0.5, and times 1 (a true distance) for comparison -- and a wall rule of 0.8 mm and a gap rule of 0.4 mm: a wall is a length and
# not a number of the field, so each must be measured and made the same.
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

from mfgkit import *

t = Table('n7_fields_that_are_not_distances: a fin of 0.6 mm and a slot of 0.2 mm (a small scene, 12 cells to the millimetre) whose field is not a distance -- the field of the part times 2 (a slope of 2), times 0.5, and times 1 (a true distance) for comparison -- and a wall rule of 0.8 mm and a gap rule of 0.4 mm: a wall is a length and not a number of the field, so each must be measured and made the same')

def make(s):
    fin = box((0, 0, 0), (0.6, 6, 4))
    return union(union(fin, box((3, 0, 0), (6, 6, 4))), box((6.2, 0, 0), (9.2, 6, 4))) * s
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
rules = [manufacturing_constraint(min_wall=0.8, min_gap=0.4)]
lo, hi = (-1.5, -1.5, -1.5), (10.7, 7.5, 5.5)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=12)
last = None
for s in (2.0, 0.5, 1.0):
    part = make(s)
    before = manufacturing_analysis(part, constraints=rules, samples=6000)
    m = margin_at(before, 'min_wall', [(0.0, 3, 2), (0.6, 3, 2)])
    g = margin_at(before, 'min_gap', [(6.0, 3, 2), (6.2, 3, 2)])
    t.check('field times %g: the check marks both faces of the fin (%.3f / %.3f; 0.8 - 0.6 - 0.04 = 0.16 by hand) and both sides of the slot (%.3f / %.3f; 0.4 - 0.2 - 0.02 = 0.18)' % (s, m[0], m[1], g[0], g[1]),
            m[0] > 0.1 and m[1] > 0.1 and g[0] > 0.1 and g[1] > 0.1, '%.3f / %.3f / %.3f / %.3f' % (m[0], m[1], g[0], g[1]), '~ 0.16, 0.18')
    res = manufacturing_optimization(part, constraints=rules, max_change=3.0)
    print('field times %g:' % s)
    print(res)
    t.near('field times %g: the fin is made 0.8 mm thick' % s, sum(run_length(res, (0.3, 3, 2), (1, 0, 0), 4.0, 6000)), 0.8, 0.05, ' mm')
    t.check('field times %g: the slot is closed (material at its middle)' % s, evaluate(res, [(6.1, 3, 2)])[0] <= 0, '', '')
    a, r, _ = changed_volumes(part, res, lo, hi, n_coarse=200000, n_dense=200000)
    t.near('field times %g: the material added, fin 0.2 x 6 x 4 and slot 0.2 x 6 x 4 = 9.6' % s, a, 9.6, 2.0, ' mm3')
    after = manufacturing_analysis(res, constraints=rules, samples=6000)
    t.check('field times %g: the independent check of the result finds no broken rule (%s)' % (s, {k: round(v['share'], 4) for k, v in after.report.items() if v['broken']}), after.compliant, '', 'none')
    last = (res, part)

t.summary()
result = last[0]
original = aside(last[1], lo, hi)[0]
result
original
