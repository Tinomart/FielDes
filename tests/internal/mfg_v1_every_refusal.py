# Every refusal of the constraint and of the tool: an overhang of 90 or -5 degrees, a draft of 0 or 45, a wall of -1 mm or of "thin", a direction of
# (0, 0, 0) or of two numbers, a support without an overhang or of a kind that does not exist, a negative clearance, a fix that does not exist, no
# constraint, an empty part: each says what is wrong and what is allowed.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                      - gaps: _remedy_gaps (a narrow gap is closed, or made as wide as asked)
#                      - draft: _remedy_draft (a wall that leans too little is offset in proportion to its depth)
#                      - undercut: _remedy_undercut (what lies under and over the part along the direction is filled)
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                      - supports: _supports_field (a block under what is left leaning out)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table('v1_every_refusal: every refusal of the constraint and of the tool: an overhang of 90 or -5 degrees, a draft of 0 or 45, a wall of -1 mm or of "thin", a direction of (0, 0, 0) or of two numbers, a support without an overhang or of a kind that does not exist, a negative clearance, a fix that does not exist, no constraint, an empty part: each says what is wrong and what is allowed')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = box((0, 0, 0), (10, 10, 10))
empty = intersection(box((0, 0, 0), (1, 1, 1)), box((5, 5, 5), (6, 6, 6)))
lo, hi = (-3, -3, -3), (13, 13, 13)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
cases = [
    ('overhang of 90 degrees', lambda: manufacturing_constraint(overhang=90), ['overhang', '90', 'not 90']),
    ('overhang of -5 degrees', lambda: manufacturing_constraint(overhang=-5), ['overhang', 'from 0']),
    ('draft of 0 degrees', lambda: manufacturing_constraint(draft=0), ['draft', 'more than 0', '45']),
    ('draft of 45 degrees', lambda: manufacturing_constraint(draft=45), ['draft', 'less than 45']),
    ('a wall of -1 mm', lambda: manufacturing_constraint(min_wall=-1), ['min_wall', 'more than 0']),
    ('a wall that is not a number', lambda: manufacturing_constraint(min_wall='thin'), ['min_wall', 'number']),
    ('an undercut direction of (0, 0, 0)', lambda: manufacturing_constraint(undercut_along=(0, 0, 0)), ['undercut_along', '(0, 0, 0)']),
    ('a build direction of two numbers', lambda: manufacturing_constraint(overhang=45, build_direction=(1, 2)), ['build_direction', 'three numbers']),
    ('a support without an overhang rule', lambda: manufacturing_constraint(support='block'), ['support', 'overhang']),
    ('a support of a kind that does not exist', lambda: manufacturing_constraint(overhang=45, support='tree'), ['support', "'block'"]),
    ('a negative clearance of a support', lambda: manufacturing_constraint(overhang=45, support='block', support_gap=-1), ['support_gap', '0 or more']),
    ('an overhang fix that does not exist', lambda: manufacturing_constraint(overhang=45, overhang_fix='drip'), ["'fill'", "'cut'"]),
    ('a gap fix that does not exist', lambda: manufacturing_constraint(min_gap=0.4, gap_fix='squeeze'), ["'fill'", "'widen'"]),
    ('no constraint to the tool', lambda: manufacturing_optimization(part, constraints=[]), ['at least one constraint']),
    ('no constraint to the check', lambda: manufacturing_analysis(part, constraints=[]), ['at least one constraint']),
    ('an empty part', lambda: manufacturing_optimization(empty, constraints=[manufacturing_constraint(min_wall=0.8)]), ['empty']),
]
for what, call, words in cases:
    message = None
    try:
        call()
    except Exception as e:
        message = str(e)
    t.check('%s is refused' % what, message is not None, 'accepted' if message is None else message[:70], 'a refusal')
    if message is not None:
        t.check('%s: the message says what is wrong and what is allowed (%s)' % (what, ' / '.join(words)), all(w in message for w in words), message[:150], 'contains ' + ', '.join(words))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
ok = manufacturing_constraint(overhang=45, min_wall=0.8, draft=2.0, support='block')
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('and a valid constraint is not refused', repr(ok).startswith('manufacturing_constraint('), repr(ok)[:60], 'a constraint')

t.summary()
result = part
original = aside(part, lo, hi)[0]
result
original
