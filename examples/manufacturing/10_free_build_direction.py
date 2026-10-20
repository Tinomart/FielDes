# 10  THE PART TURNS: the way up of the print is found and shown
#
# WHAT THIS SHOWS.  When the printer's rules do not say which way up the part is printed (`build_direction` left free) the tool looks for the way that leaves the least of the surface leaning out
# past the overhang angle -- the six axis directions and 40 others spread over the sphere -- and works on the part standing that way.  The result is DRAWN standing that way too (the card says
# "Way up of the print (x, y, z), part shown turned"), and `.oriented()` / an export hand it over turned.  `show_turned=False` draws it where it is.
#
# WHAT YOU SEE.  A plate on a post, as it was (right) and the result standing the way it is printed, the plate at the bottom (left).  WHAT YOU READ: one PASS line per check.  About one minute.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('10 the part turns: a plate on a post (the post under it), the way up of the print not given: the tool finds that the plate has to be at the bottom and turns the part')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
post = box((0, 0, 0), (10, 10, 30))
plate = box((-20, -20, 30), (30, 30, 35))
part = union(post, plate)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
free = manufacturing_constraint(overhang=45, build_direction=None)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[free], max_change=0.9)
lo, hi = (-23, -23, -38), (33, 33, 38)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=2)
d = res.build_direction
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the way up found is (0, 0, -1)', abs(d[0]) < 0.05 and abs(d[1]) < 0.05 and d[2] < -0.95, tuple(round(c, 3) for c in d), '(0, 0, -1)')
a, r, _ = changed_volumes(part, res, (-23, -23, -3), (33, 33, 38))
t.check('turned the right way up there is no overhang: nothing added, nothing cut', a < 2.0 and r < 2.0, '+%.3g -%.3g mm3' % (a, r), 0)
turned = res.oriented()
pts = points_in((-40, -40, -32.5), (40, 40, -32.5), 60000, seed=4)
area_plate = 6400.0 * sum(1 for v in evaluate(turned, pts) if v <= 0) / 60000.0
pts = points_in((-40, -40, -10), (40, 40, -10), 60000, seed=5)
area_post = 6400.0 * sum(1 for v in evaluate(turned, pts) if v <= 0) / 60000.0
t.near('the turned part: the cross-section at the bottom (z = -32.5) is the plate, 50 x 50', area_plate, 2500.0, 200.0, ' mm2')
t.near('the turned part: the cross-section higher up (z = -10) is the post, 10 x 10', area_post, 100.0, 40.0, ' mm2')
pts = points_in((-40, -40, -34.9), (40, 40, -34.9), 60000, seed=6)
area_floor = 6400.0 * sum(1 for v in evaluate(turned, pts) if v <= 0) / 60000.0
t.near('the turned part: the lowest layer (z = -34.9) is the whole plate, 50 x 50: it stands on its plate', area_floor, 2500.0, 200.0, ' mm2')

t.summary()
result = turned
original = aside(part, lo, hi)[0]
result
original
