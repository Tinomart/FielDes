# Two blocks 2 mm apart, a 4 mm gap asked: the slot is closed, level with the faces.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - gaps: _remedy_gaps (a narrow gap is closed, or made as wide as asked)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('g1_slot_fill: two blocks 2 mm apart, a 4 mm gap asked: the slot is closed, level with the faces')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (12, 16, 18)), box((14, 0, 0), (26, 16, 18)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_gap=4.0)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
lo, hi = (-3, -3, -3), (29, 19, 21)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
generic_checks(t, part, res, [rule], lo, hi, added=2 * 16 * 18, reach=1.15, within=((12, 0, 0), (14, 16, 18)), name='slot')
behind, ahead = run_length(res, (6, 8, 9), (1, 0, 0), 40.0, 8000)
t.near('slot: the two blocks are one piece, 26 mm along x (tape through the slot)', behind + ahead, 26.0, 0.1, ' mm')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
