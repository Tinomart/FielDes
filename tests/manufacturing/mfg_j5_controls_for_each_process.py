# J5, J6: controls for each process: a small part built by hand to the published rules (an L-bracket of 2 mm walls for the printer, a plate with a
# window of 3.2 mm corners for the mill, a drafted open box for the mould) must be found fine by the judge and left alone by the tool; the same part
# with one defect for each rule (a rib of 0.4 mm, a ledge leaning 70 degrees, a slot of 0.2 mm, a sharp corner, a groove, a wall without draft) must
# be found broken by the judge, and fixed or listed by the tool.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - (the checks of the part: see below)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('j5_controls_for_each_process: J5, J6: controls for each process: a small part built by hand to the published rules (an L-bracket of 2 mm walls for the printer, a plate with a window of 3.2 mm corners for the mill, a drafted open box for the mould) must be found fine by the judge and left alone by the tool; the same part with one defect for each rule (a rib of 0.4 mm, a ledge leaning 70 degrees, a slot of 0.2 mm, a sharp corner, a groove, a wall without draft) must be found broken by the judge, and fixed or listed by the tool')

lo, hi = (-8, -8, -4), (50, 38, 30)
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
results = []
row = 0.0
for process in ('fff', 'cnc', 'mould'):
    name, make_rule, rules = _CONTROL_RULES[process]
    rule = make_rule()
    good = _control_parts(process, False)
    bad = _control_parts(process, True)
    res_good = manufacturing_optimization(good, constraints=[rule], max_change=0.25, max_support_height=10)
    before_good = job_shares(good, lo, hi, 1500, rules)
    t.check('%s, positive control: the judge finds nothing broken before (%s)' % (name, {k: round(v, 3) for k, v in before_good.items() if k != 'points'}), all(before_good[k] <= 0.005 for k in rules if k in before_good), '', 'every share <= 0.5 %')
    t.check('%s, positive control: the tool leaves it alone (it changed %.2f %% of the volume)' % (name, 100.0 * res_good.share), res_good.share < 0.01, '%.4f' % res_good.share, '< 0.01')
    res_bad = manufacturing_optimization(bad, constraints=[rule], max_change=0.25, max_support_height=10)
    print(name, 'negative control:', res_bad)
    before, after = job_table(t, '%s, negative control' % name, bad, res_bad, lo, hi, rules, count=1500)
    found = [k for k in rules if k in before and before[k] > 0.005]
    t.check('%s, negative control: the judge finds what was put in broken before (%s)' % (name, ', '.join('%s %.1f %%' % (k, 100.0 * before[k]) for k in found)), len(found) >= 2, '%d rules found' % len(found), '>= 2')
    results.append(res_bad)

t.summary()
result = results[0]
original = aside(_control_parts('fff', True), lo, hi)[0]
result
original
