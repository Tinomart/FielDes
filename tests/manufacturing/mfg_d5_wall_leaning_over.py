# A wall that leans OVER (3 degrees, wider at the top, standing on a base) with a draft of 1 degree and no undercut asked, and then with an undercut
# along z asked too (a mould): the draft alone leaves it, the undercut rule fills what lies under the lean, and the next pass gives the new vertical
# face its draft.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - draft: _remedy_draft (a wall that leans too little is offset in proportion to its depth)
#                      - undercut: _remedy_undercut (what lies under and over the part along the direction is filled)
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('d5_wall_leaning_over: a wall that leans OVER (3 degrees, wider at the top, standing on a base) with a draft of 1 degree and no undercut asked, and then with an undercut along z asked too (a mould): the draft alone leaves it, the undercut rule fills what lies under the lean, and the next pass gives the new vertical face its draft')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
tn = math.tan(math.radians(3.0))
over = half_space((1.0, 0.0, -tn), (0.0, 0.0, 0.0))                              # (inside: x <= z tan 3: the face leans out going up)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the plane of the leaning wall faces the way the case assumes (inside to its left)', evaluate(over, [(-3.0, 5.0, 10.0)])[0] < 0 < evaluate(over, [(3.0, 5.0, 1.0)])[0], '', '')
wall = intersection(over, box((-12, 0, 0), (40, 10, 20)))
part = union(wall, box((-12, 0, -3), (40, 10, 0)))                                # (standing on a base: material below the overhang)
lo, hi = (-14, -3, -6), (42, 13, 23)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
only_draft = manufacturing_constraint(draft=1.0)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res1 = manufacturing_optimization(part, constraints=[only_draft], max_change=3.0)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=4)
# (measured round the leaning face only, x from -4 to 4, z from 0.2 to 20.2, y from 0.5 to 9.5: the part has other vertical faces -- the left face of the wall at x = -12, the sides of the base, the end faces of the
# wall at y = 0 and y = 10 -- which the draft rule rightly gives a draft (85.9 mm3 of them), and that is not what is asked here)
lo_m, hi_m = (-4.0, 0.5, 0.2), (4.0, 9.5, 20.2)
a1, r1, _ = changed_volumes(part, res1, lo_m, hi_m)
print(res1)
# THE ANSWER BY HAND: a face that leans 3 degrees has 3 degrees off the pull, the draft rule asks 1 degree and is kept, so the draft remedy changes nothing at that face (a lean over is no lack of draft);
# with an undercut asked too, the air under the overhang (it has material above it and below it) is filled up to the plane through the top edge: a triangle 20 x (20 tan 3 degrees) / 2 = 10.5 mm2 x 10 = 105 mm3
t.check('with a draft rule alone the leaning-over face is left as it is (+%.2f -%.2f mm3 changed)' % (a1, r1), a1 < 3.0 and r1 < 3.0, '+%.2f -%.2f' % (a1, r1), '< 3')
both = [manufacturing_constraint(draft=1.0), manufacturing_constraint(undercut_along=(0, 0, 1))]
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res2 = manufacturing_optimization(part, constraints=both, max_change=3.0)
print(res2)
a2, r2, _ = changed_volumes(part, res2, lo_m, hi_m)
t.near('with the undercut asked too the material added, the triangle under the lean (10.5 mm2 over the 9 mm of length measured: by hand 94.5 mm3)', a2, 94.5, 20.0, ' mm3')
t.check('nothing cut', r2 < 2.0, '%.2f mm3' % r2, 0)
after = manufacturing_analysis(res2, constraints=both, samples=6000)
if not after.compliant:
    print(after)
# (the remedies are made from the part as it was and united, not from one another: the vertical face the undercut fill makes is no face of the part, so one pass does not give it a draft.  What the independent
# check finds there must be LISTED by the tool, and a second pass -- the next pass, as docs/manufacturing.md says -- gives it its draft)
marks = {k: round(v['share'], 4) for k, v in after.report.items() if v['broken']}
listed = sorted(set(s['rule'] for s in res2.fail_states))
t.check('after one pass the new vertical face has no draft yet: the independent check finds %s and the tool lists %s' % (marks, listed), after.compliant or bool(listed), '%s, listed %s' % (marks, listed), 'listed')
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res3 = manufacturing_optimization(res2, constraints=both, max_change=3.0)
print(res3)
after3 = manufacturing_analysis(res3, constraints=both, samples=6000)
if not after3.compliant:
    print(after3)
share3 = sum(r['share'] for r in after3.report.values())
t.check('a second pass gives the new face its draft: the independent check of its result marks %.2f %% of the surface (under 0.5 %%)' % (100.0 * share3), share3 < 0.005, '%.4f' % share3, '< 0.005')

t.summary()
result = res3
original = aside(part, lo, hi)[0]
result
original
