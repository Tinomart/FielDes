# Parametric optimization against the known best design of a cantilever (beam theory).
#
# THE PROBLEM.  A beam of aluminium, 100 mm long, held at x = 0 and pulled down at its free end with 200 N.  Its width and height are var()s.  Make it as light as possible
# while it stays under 90 MPa and bends less than 0.5 mm at its end.  The search starts from a design that is too heavy: 20 x 12 mm.
#
# THE EXACT ANSWER (derived here, no outside source claimed).  The end of a beam bends by  P L^3 / (3 E I)  with  I = b h^3 / 12,  that is  4 P L^3 / (E b h^3).  Setting that to the
# limit of 0.5 mm: the lightest beam is as narrow as the range allows (the lower bound of the width, 8 mm) and just deep enough, at  h = (4 P L^3 / (E b delta))^(1/3)  (14.26 mm).
# The stress limit does not decide: the bending stress there is 6 P L / (b h^2) = 74 MPa, under 90.
#
# WHAT THE SCRIPT DOES: runs parametric_optimization on the beam (each design is a real analysis on tetrahedra), prints the design it found beside the exact one, and stops with an
# AssertionError when the height is more than 4 % off, the width is not at its lower bound or the mass is more than 4 % off.  A mesh of tetrahedra is a little stiffer or softer than the beam
# theory says (see v1_cantilever_beam.py), which is what the tolerance is for.  About a minute.
from fieldes import *

P, L, E = 200.0, 100.0, aluminium.E
rho = aluminium.density * 1000.0                      # kg per mm^3
width_low, delta_max = 8.0, 0.5
height_exact = (4.0 * P * L ** 3 / (E * width_low * delta_max)) ** (1.0 / 3.0)
mass_exact = rho * width_low * height_exact * L
TOLERANCE = 0.04
print('starting design : width 20, height 12     -> mass %.5f kg' % (rho * 20 * 12 * L))
print('exact best      : width %.1f, height %.2f -> mass %.5f kg' % (width_low, height_exact, mass_exact))

width = var(20)
height = var(12)
beam = box((100, width, height), position=(50, width / 2, height / 2))
wall = fixed(box((2, 61, 61), position=(0, 29.5, 29.5)))
pull = force(box((2, 61, 61), position=(100, 29.5, 29.5)), (0, 0, -200))
stress = static_analysis(beam, supports=[wall], loads=[pull], material=aluminium, element_size=4)
mass_1 = stress.mass
max_von_mises_1 = stress.max_von_mises
max_displacement_1 = stress.max_displacement
best = parametric_optimization(stress, [(width, 8, 30), (height, 6, 24)], conditions=[minimize(mass_1), keep_below(max_von_mises_1, 90), keep_below(max_displacement_1, 0.5)])
w, h = best.values['width'], best.values['height']
print('found           : width %.2f, height %.2f -> mass %.5f kg  (%d designs tried, feasible: %s)' % (w, h, rho * w * h * L, best.evaluations, best.feasible))
print('height %+.1f %% from the exact one, mass %+.1f %%' % (100 * (h / height_exact - 1), 100 * (rho * w * h * L / mass_exact - 1)))
if not best.feasible:
    raise AssertionError('the best design breaks a limit')
if abs(w - width_low) > 0.3:
    raise AssertionError('the width should be at its lower bound, %g mm: it is %.2f' % (width_low, w))
if abs(h / height_exact - 1) > TOLERANCE:
    raise AssertionError('the height is %.1f %% from the beam theory: more than %.0f %%' % (100 * (h / height_exact - 1), 100 * TOLERANCE))
if abs(rho * w * h * L / mass_exact - 1) > TOLERANCE:
    raise AssertionError('the mass is more than %.0f %% from the exact one' % (100 * TOLERANCE))
print('OK')
