# A shelf tilted 30 degrees (a plate 40 x 30 x 3 with a post standing square on it), an overhang of 45 degrees and the way up left free: of the six
# axes none holds it without an overhang, and the direction the tool finds, searched over the axes and forty more, leaves less of the surface leaning
# out than the best axis does.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                      - the way up: _direction_of (the one that leaves the least of the surface leaning out)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('b2_tilted_shelf: a shelf tilted 30 degrees (a plate 40 x 30 x 3 with a post standing square on it), an overhang of 45 degrees and the way up left free: of the six axes none holds it without an overhang, and the direction the tool finds, searched over the axes and forty more, leaves less of the surface leaning out than the best axis does')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
shelf = union(box((0, 0, 0), (40, 30, 3)), cylinder(4.0, 20.0, (20, 15, 3)))
part = rotate_y(shelf, math.radians(30.0), center=(20, 15, 0))
lo, hi = (-8, -3, -16), (48, 33, 27)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
axes = {'+z': (0, 0, 1), '-z': (0, 0, -1), '+x': (1, 0, 0), '-x': (-1, 0, 0), '+y': (0, 1, 0), '-y': (0, -1, 0)}
shares = {}
for name, d in axes.items():
    rep = manufacturing_analysis(part, constraints=[manufacturing_constraint(overhang=45, build_direction=d)], samples=7000)
    shares[name] = rep.report['overhang']['share']
free = manufacturing_analysis(part, constraints=[manufacturing_constraint(overhang=45, build_direction=None)], samples=7000)
found = free.orientation
print(free)
print('shares of the surface that lean out, by axis: %s; the direction found: (%.3f, %.3f, %.3f), %.4f' % ({k: round(v, 4) for k, v in shares.items()}, found[0], found[1], found[2], free.report['overhang']['share']))
best_axis = min(shares.values())
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('none of the six axes holds the shelf without an overhang (the best axis leaves %.3g of the surface leaning out)' % best_axis, best_axis > 0.02, '%.4f' % best_axis, '> 0.02')
# (the search is "a start, not a search for the best": 6 axes and 40 directions about 30 degrees apart, and for a way up that is not an axis the plate is taken at the lowest corner of the part's box, so a face
# laid on the plate at an angle does not count as resting on it; the honest property is that it does better than the best axis, not that it finds the tilt of the shelf)
t.check('the direction found leaves less of the surface leaning out than the best axis (%.4f against %.4f)' % (free.report['overhang']['share'], best_axis), free.report['overhang']['share'] < best_axis, '%.4f' % free.report['overhang']['share'], '< %.4f' % best_axis)
t.check('the direction found is a unit vector, and not an axis (the search went beyond the axes)', abs(sum(c * c for c in found) - 1.0) < 1e-6 and max(abs(c) for c in found) < 0.999, tuple(round(c, 3) for c in found), 'oblique')
again = manufacturing_analysis(part, constraints=[manufacturing_constraint(overhang=45, build_direction=found)], samples=7000)
t.near('asked in the direction it found, the check gives the same share of the surface', again.report['overhang']['share'], free.report['overhang']['share'], 0.02, '')

t.summary()
result = free
original = aside(part, lo, hi)[0]
result
original
