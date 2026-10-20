# A plate 24 x 24 x 4 with a grit texture (bumps and pits 0.3 mm high, 1.5 mm across) on every face, an inside corner radius of 1 mm and a wall of 0.8
# asked: a surface that is nowhere flat; the tool must not run away -- nothing added farther than 0.6 mm from the part, the texture still a texture.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                      - corners: the library fillet (a ball rolls in the inside corner)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('x5_grit_texture: a plate 24 x 24 x 4 with a grit texture (bumps and pits 0.3 mm high, 1.5 mm across) on every face, an inside corner radius of 1 mm and a wall of 0.8 asked: a surface that is nowhere flat; the tool must not run away -- nothing added farther than 0.6 mm from the part, the texture still a texture')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = texture(box((0, 0, 0), (24, 24, 4)), 'grit', depth=0.3, size=1.5)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(min_radius=1.0, min_wall=0.8)
lo, hi = (-2, -2, -2), (26, 26, 6)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=5)
started = time.time()
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_change=0.5)
seconds = time.time() - started
print(res)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the tool ran in %.0f s (under 400)' % seconds, seconds < 400, '%.0f s' % seconds, '< 400')
cmp_ = compare(part, res, lo, hi, n=240000)
far = debris(cmp_, 0.6)
t.check('nothing added farther than 0.6 mm from the part (twice the depth of the texture): %d points' % len(far), len(far) == 0, '%d points' % len(far), 0)
t.check('no piece of added material floats beside the plate', not floating_clusters(cmp_, cell=1.0), '', 0)
a, r, _ = changed_volumes(part, res, lo, hi, n_coarse=240000, n_dense=240000)
layer = (2 * 24 * 24 + 4 * 24 * 4) * 0.3
t.check('the change is small: %.0f mm3 added and %.0f cut, under %.0f (a layer as thick as the texture over the whole surface)' % (a, r, layer), a + r < layer, '+%.0f -%.0f' % (a, r), '< %.0f' % layer)
after = manufacturing_analysis(res, constraints=[rule], samples=6000)
before = manufacturing_analysis(part, constraints=[rule], samples=6000)
for rname in ('min_radius', 'min_wall'):
    t.check('the share of the surface breaking %s did not grow (%.3g before, %.3g after)' % (rname, before.report[rname]['share'], after.report[rname]['share']), after.report[rname]['share'] <= before.report[rname]['share'] + 0.002, '%.4g' % after.report[rname]['share'], '<= %.4g' % (before.report[rname]['share'] + 0.002))

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
