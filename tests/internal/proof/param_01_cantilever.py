# Parametric optimization 1: does it find the known best design of a cantilever?
#
# THE PROBLEM.  A beam held at x = 0 and pulled down at its end with 200 N.  Its width and height are var()s.  Make it as light as possible
# while it stays under 90 MPa and bends less than 0.5 mm.  The search starts from a design that is too heavy: 20 x 12 mm.
#
# THE KNOWN BEST CASE (beam theory).  The tip bends by  P L^3 / (3 E I)  with  I = b h^3 / 12,  that is  4 P L^3 / (E b h^3).  Setting that to
# 0.5 mm: the lightest beam is as narrow as the range allows (width 8, the lower bound) and just deep enough, at the height printed below
# (about 14.3 mm).  The 90 MPa limit is not what decides: the displacement limit is.
#
# WHAT TO LOOK AT.  Three beams in a row along y: the starting design (front, plain), the optimizer's answer (middle, coloured by stress)
# and the known best design from beam theory (back, plain).  The numbers it found are in the result card and in the output pane
# (best design, and the list of every design that was tried).  Every design is a real solve (the results are cached like every analysis: running the script again reads them back; to solve everything again, delete the result cache, see README.txt).
import math
from fieldes import *

view.set_bounds([-5, -50, -5], [105, 50, 30], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

P, L, E = 200.0, 100.0, aluminium.E
rho = aluminium.density * 1000.0                      # kg per mm^3
width_low, delta_max = 8.0, 0.5
height_known = (4.0 * P * L ** 3 / (E * width_low * delta_max)) ** (1.0 / 3.0)
print('STARTING design : width 20, height 12      -> mass %.5f kg' % (rho * 20 * 12 * L))
print('KNOWN BEST      : width %.1f, height %.2f  -> mass %.5f kg  (beam theory)' % (width_low, height_known, rho * width_low * height_known * L))

width = var(20)
height = var(12)
beam = box((0, 0, 0), (100, width, height))

wall = fixed(box((-1, -1, -1), (1, 60, 60)))
pull = force(box((99, -1, -1), (101, 60, 60)), (0, 0, -200))
stress = static_analysis(beam, supports=[wall], loads=[pull], material=aluminium, element_size=4)

best = parametric_optimization(stress, [(width, 8, 30), (height, 6, 24)],
                               constraints=[minimize('mass'), 'max_von_mises <= 90', 'max_displacement <= 0.5'])

starting_beam = move(box((0, 0, 0), (100, 20, 12)), (0, -35, 0))
known_best_beam = move(box((0, 0, 0), (100, width_low, height_known)), (0, 25, 0))
starting_beam
best
known_best_beam
