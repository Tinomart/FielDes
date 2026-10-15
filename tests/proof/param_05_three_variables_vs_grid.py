# Parametric optimization 5: three variables, against a brute-force grid.
#
# THE PROBLEM.  A beam with a round hole through its depth, held at x = 0 and pulled down at its end with 200 N.  Three var()s: the width,
# the height and the radius of the hole.  Make it as light as possible while it stays under 120 MPa and bends less than 0.8 mm.  The search
# starts from a beam that is far too heavy: 40 wide, 20 high, with a hole of radius 2.
#
# THE REFERENCE.  There is no formula for the best beam with a hole, so the reference is brute force: first this script solves a grid of
# 5 x 5 x 5 = 125 beams (each a real solve) and prints the lightest of them that keeps the limits.  The grid is coarse, so its best is not the
# true best: a good search should end at a beam at least as light as the grid's.  That is what is compared.
#
# WHAT TO LOOK AT.  Three beams in a row: the starting beam (front, plain), the optimizer's answer (middle, coloured by stress) and the best
# beam of the grid (back, plain).  The output pane prints the grid's best first, then the optimizer's list of designs.
import itertools
from fieldes import *

view.set_bounds([-5, -75, -5], [105, 75, 30], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

wall = fixed(box((-1, -1, -1), (1, 60, 60)))
pull = force(box((99, -1, -1), (101, 60, 60)), (0, 0, -200))


def beam_of(w, h, r):
    return difference(box((0, 0, 0), (100, w, h)), cylinder(r, 40, (50, 15, -10)))


grid_best, solved = None, 0
for w, h, r in itertools.product([20, 25, 30, 35, 40], [8, 11, 14, 17, 20], [2, 3.5, 5, 6.5, 8]):
    try:
        s = static_analysis(beam_of(w, h, r), supports=[wall], loads=[pull], material=aluminium, element_size=3)
    except Exception:
        continue
    solved += 1
    if s.max_von_mises <= 120 and s.max_displacement <= 0.8 and (grid_best is None or s.mass < grid_best[0]):
        grid_best = (s.mass, w, h, r, s.max_von_mises, s.max_displacement)
if grid_best is None:
    print('GRID REFERENCE: %d designs solved, none keeps the limits' % solved)
else:
    print('GRID REFERENCE: %d designs solved; the lightest that keeps the limits: width %g, height %g, hole radius %g -> mass %.5f kg, %.1f MPa, %.3f mm'
          % (solved, grid_best[1], grid_best[2], grid_best[3], grid_best[0], grid_best[4], grid_best[5]))
print('STARTING design : width 40, height 20, hole radius 2')

width = var(40)
height = var(20)
radius = var(2)
beam = difference(box((0, 0, 0), (100, width, height)), cylinder(radius, 40, (50, 15, -10)))
stress = static_analysis(beam, supports=[wall], loads=[pull], material=aluminium, element_size=3)

best = parametric_optimization(stress, [(width, 20, 40), (height, 8, 20), (radius, 2, 8)],
                               minimize='mass',
                               constraints=['max_von_mises <= 120', 'max_displacement <= 0.8'])

starting_beam = move(beam_of(40, 20, 2), (0, -60, 0))
starting_beam
best
if grid_best is not None:
    grid_best_beam = move(beam_of(grid_best[1], grid_best[2], grid_best[3]), (0, 60, 0))
    grid_best_beam
