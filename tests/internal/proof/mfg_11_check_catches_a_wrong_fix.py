# Manufacturing 11: does the check catch a fix that is wrong?  (The check has to be independent of the optimization, or it can only agree with it.)
#
# A fin 1.5 mm thick, for a rule that a wall is at least 4 mm.  Six versions of it stand in a row, and every one is given to manufacturing_analysis:
#
#   1  the fin as it is (1.5 mm)                           the check should say the wall is too thin: 2.5 mm short
#   2  a "fix" made by hand that is too small: 3.0 mm      should be flagged (25 % short, more than the 5 % the check allows)
#   3  a hand-made 3.7 mm                                  should be flagged (7.5 % short)
#   4  a hand-made 3.9 mm                                  should pass (2.5 % short: within the 5 % that a measurement is allowed to be off)
#   5  a hand-made 4.0 mm, drawn by me, not by the tool    should pass
#   6  what manufacturing_optimization makes of the fin    should pass
#
# Versions 2 to 5 are plain boxes written here: nothing of the optimization made them, so what the check says of them is its own judgment.  The check
# measures a wall by the shortest of nine lines through it (manufacturing_check.py); the optimization finds a thin wall by the line along the normal
# (manufacturing.py).  The two files share no code that decides what is broken.
#
# The output pane prints the report of each, with the share of the surface that breaks the rule.  There are no PASS or FAIL lines: compare each report with
# the expectation above.  In the viewport, red is where the rule is broken, grey where it is kept.
from fieldes import *

view.set_bounds([-10, -10, -5], [95, 30, 30], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

rule = manufacturing_constraint(min_wall=4.0)
original = box((0, 0, 0), (1.5, 16, 24))
by_hand = [box((-0.75, 0, 0), (2.25, 16, 24)), box((-1.1, 0, 0), (2.6, 16, 24)), box((-1.2, 0, 0), (2.7, 16, 24)), box((-1.25, 0, 0), (2.75, 16, 24))]
made = manufacturing_optimization(original, constraints=[rule], max_change=3.0)

row = [original] + by_hand + [made]
names = ['1  as it is, 1.5 mm', '2  by hand, 3.0 mm', '3  by hand, 3.7 mm', '4  by hand, 3.9 mm', '5  by hand, 4.0 mm', '6  made by manufacturing_optimization']
reports = []
for i, (shape, name) in enumerate(zip(row, names)):
    r = manufacturing_analysis(move(shape, (15 * i, 0, 0)), constraints=[rule])
    print('=== ' + name)
    print(r)
    reports.append(r)
reports[0]
reports[1]
reports[2]
reports[3]
reports[4]
reports[5]
