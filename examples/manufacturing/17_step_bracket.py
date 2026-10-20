# 17  A REAL PART: a bracket from a STEP file made ready for a printer
#
# WHAT THIS SHOWS.  A bracket that was designed to be machined, not printed, imported from its STEP file and checked against a printer with a 0.4 mm nozzle that also asks for no overhang over 45
# degrees.  The tool changes only what breaks a rule and leaves the rest as it was.
#
# WHAT YOU SEE.  The part as it was (right) and the result (left): red where material was added (gussets under what overhangs).  WHAT YOU READ: one PASS line per check, and the share of the surface
# that breaks each rule before and after.

import sys, os
sys.path.append(os.path.join(os.getcwd(), 'kit'))
from mfgkit import *

t = Table('17 a real part: the STEP bracket of the examples, for a printer with a 0.4 mm nozzle that also asks for no overhang over 45 degrees')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
parts = import_model(_step_file('PivotBearingSupportBracket.STEP'))
bracket, (blo, bhi) = parts[0]
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = fff_constraint(nozzle=0.4, overhang=45)
print(manufacturing_analysis(bracket, constraints=[rule]))
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(bracket, constraints=[rule], max_support_height=10)
print(res)
lo, hi = tuple(c - 3 for c in blo), tuple(c + 3 for c in bhi)
shift = 1.2 * (hi[0] - lo[0])
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([lo[0] - 5, lo[1] - 5, lo[2] - 5], [hi[0] + shift + 5, hi[1] + 5, hi[2] + 5], auto_scale=True)
view.set_resolution(1)
view.set_quality(8)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
complicated_checks(t, bracket, res, rule, lo, hi, 'step bracket', n=30000)

t.summary()
result = res
original = move(bracket, (shift, 0, 0))
result
original
