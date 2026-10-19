# A fin 0.6 mm thick and a slot of 0.2 mm between two blocks, a wall rule of 0.8 and a gap rule of 0.4 asked, and the tool told which remedy it may
# make (remedies=): only the one chosen is made; a remedy that no rule asks for makes nothing and the tool says so; a name that is no remedy is
# refused and the message lists those there are.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                      - gaps: _remedy_gaps (a narrow gap is closed, or made as wide as asked)
#                      - draft: _remedy_draft (a wall that leans too little is offset in proportion to its depth)
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('h4_remedy_selection: a fin 0.6 mm thick and a slot of 0.2 mm between two blocks, a wall rule of 0.8 and a gap rule of 0.4 asked, and the tool told which remedy it may make (remedies=): only the one chosen is made; a remedy that no rule asks for makes nothing and the tool says so; a name that is no remedy is refused and the message lists those there are')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
fin = box((0, 0, 0), (0.6, 12, 10))
part = union(union(fin, box((6, 0, 0), (12, 12, 10))), box((12.2, 0, 0), (18.2, 12, 10)))
rules = [manufacturing_constraint(min_wall=0.8, min_gap=0.4)]
lo, hi = (-3, -3, -3), (21, 15, 13)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=8)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
only_walls = manufacturing_optimization(part, constraints=rules, remedies=['walls'], max_change=3.0)
print(only_walls)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('remedies=[walls]: only the wall remedy is on the list of what was done', [c['remedy'] for c in only_walls.changes] == ['walls'], [c['remedy'] for c in only_walls.changes], "['walls']")
t.near('remedies=[walls]: the fin is thickened to 0.8', sum(run_length(only_walls, (0.3, 6, 5), (1, 0, 0), 4.0, 6000)), 0.8, 0.05, ' mm')
t.near('remedies=[walls]: the slot between the blocks is as it was, 0.2 mm', sum(run_length(only_walls, (12.1, 6, 5), (1, 0, 0), 2.0, 6000, inside=False)), 0.2, 0.02, ' mm')
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
only_gaps = manufacturing_optimization(part, constraints=rules, remedies=['gaps'], max_change=3.0)
print(only_gaps)
t.check('remedies=[gaps]: only the gap remedy is on the list', [c['remedy'] for c in only_gaps.changes] == ['gaps'], [c['remedy'] for c in only_gaps.changes], "['gaps']")
t.near('remedies=[gaps]: the fin is as it was, 0.6', sum(run_length(only_gaps, (0.3, 6, 5), (1, 0, 0), 4.0, 6000)), 0.6, 0.02, ' mm')
t.check('remedies=[gaps]: the slot is closed (material at its middle)', evaluate(only_gaps, [(12.1, 6, 5)])[0] <= 0, '', '')
a1, r1, _ = changed_volumes(part, only_walls, lo, hi, n_coarse=200000, n_dense=200000)
a2, r2, _ = changed_volumes(part, only_gaps, lo, hi, n_coarse=200000, n_dense=200000)
t.near('what the wall remedy added: 0.2 x 12 x 10 = 24 mm3', a1, 24.0, 5.0, ' mm3')
t.near('what the gap remedy added: 0.2 x 12 x 10 = 24 mm3', a2, 24.0, 5.0, ' mm3')
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
unasked = manufacturing_optimization(part, constraints=rules, remedies=['corners'], max_change=3.0, verify=True)
t.check('remedies=[corners], which no rule asks for: nothing is made (%s)' % [c['remedy'] for c in unasked.changes], not unasked.changes and unasked.added == 0 and unasked.removed == 0, '%d remedies, +%.3g' % (len(unasked.changes), unasked.added), 'none')
t.check('and the tool lists the wall and the gap as broken, with the reason', {s['rule'] for s in unasked.fail_states} >= {'min_wall', 'min_gap'} and all(s['why'] for s in unasked.fail_states), [(s['rule'], s['why']) for s in unasked.fail_states][:3], 'min_wall and min_gap, each with a why')
message = ''
try:
    manufacturing_optimization(part, constraints=rules, remedies=['walls', 'paint'])
except Exception as e:                                  # (ManufacturingError)
    message = str(e)
t.check('a remedy that does not exist is refused, and the message names it and lists the six there are', "paint" in message and all(k in message for k in ('undercut', 'draft', 'gaps', 'corners', 'walls', 'overhang')), message[:160], 'names paint and the six remedies')

t.summary()
result = only_walls
original = aside(part, lo, hi)[0]
result
original
