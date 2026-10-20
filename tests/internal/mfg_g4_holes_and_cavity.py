# Closed and narrow spaces: a hollow cube with 0.5 mm walls, and holes of 0.3, 0.5, 0.62 and 1.0 mm in a block for a gap rule of 0.6 mm: the small
# ones must close, the others stay.
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

t = Table('g4_holes_and_cavity: closed and narrow spaces: a hollow cube with 0.5 mm walls, and holes of 0.3, 0.5, 0.62 and 1.0 mm in a block for a gap rule of 0.6 mm: the small ones must close, the others stay')

# (a) THE HOLLOW CUBE: a cube 24 mm with a closed cavity inside it, the walls 0.5 mm thick: the wall rule 0.8 asks for thicker walls; the cavity has to stay a cavity
cube = difference(box((0, 0, 0), (24, 24, 24)), box((0.5, 0.5, 0.5), (23.5, 23.5, 23.5)))
# (b) THE BLOCK: 30 x 12 x 10 with four round holes through it, across 0.3, 0.5, 0.62 and 1.0 mm; a gap of 0.6 asked, and the check allows 5 % (a hole under 0.57 is too narrow)
diameters = [0.3, 0.5, 0.62, 1.0]
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
block = box((50, 0, 0), (80, 12, 10))
for i, d in enumerate(diameters):
    block = difference(block, cylinder(d / 2.0, 12, (55 + 6.5 * i, 6, -1)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
wall_rule = manufacturing_constraint(min_wall=0.8)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
gap_rule = manufacturing_constraint(min_gap=0.6)
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([-5, -5, -3], [100, 30, 30], auto_scale=True)
view.set_resolution(4)
view.set_quality(8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
hollow = manufacturing_optimization(cube, constraints=[wall_rule], max_change=3.0)
lo, hi = (-3, -3, -3), (27, 27, 27)
centre_air = evaluate(hollow, [(12, 12, 12)])[0]
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('hollow cube: the cavity is still a cavity (air at its middle: %.2f)' % centre_air, centre_air > 5, '%.2f' % centre_air, '> 5')
for axis, start, direction in (('x', (0.25, 12, 12), (1, 0, 0)), ('y', (12, 0.25, 12), (0, 1, 0)), ('z', (12, 12, 0.25), (0, 0, 1))):
    b, a = run_length(hollow, start, direction, 2.0, 5000)
    t.between('hollow cube: the wall along %s measured with a tape (0.8 asked)' % axis, b + a, 0.78, 0.87, ' mm')
b, a = run_length(hollow, (23.75, 12, 12), (1, 0, 0), 2.0, 5000)
t.between('hollow cube: the opposite wall (the wall at x = 24), measured with a tape', b + a, 0.78, 0.87, ' mm')
t.near('hollow cube: the cavity shrank by what the walls grew (0.15 on each side: 23 -> 22.7 across)', sum(run_length(hollow, (12, 12, 12), (1, 0, 0), 30.0, 12000, inside=False)), 22.7, 0.1, ' mm')
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
gapped = manufacturing_optimization(block, constraints=[gap_rule], max_change=3.0)
state = []
for i, d in enumerate(diameters):
    v = evaluate(gapped, [(55 + 6.5 * i, 6, 5)])[0]
    state.append('closed' if v <= 0 else 'open')
print('holes of %s mm: %s' % (diameters, state))
t.check('holes: 0.3 and 0.5 mm are closed (too narrow for a gap of 0.6), 0.62 and 1.0 mm stay open', state == ['closed', 'closed', 'open', 'open'], state, "['closed', 'closed', 'open', 'open']")
cmp_ = compare(block, gapped, (47, -3, -3), (83, 15, 13), n=120000)
t.check('holes: nothing cut', len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
t.check('holes: nothing added farther than 0.4 mm from the block', not debris(cmp_, 0.4), '', 0)

t.summary()
result = hollow
result
