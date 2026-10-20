# Parametric optimization 4: an optimum in the MIDDLE of the range, with a known answer.
#
# The first three scripts have their best design on a bound or on a limit.  This one has it inside the range, where it can only be
# found by weighing two things against each other.
#
# THE PROBLEM.  A beam 20 mm wide, 100 mm long, held at one end and pulled down at the other with 200 N.  Only its height h is a var()
# (6 to 24 mm).  The measure to make small is   mass + k * tip deflection :  a deeper beam is heavier but bends less.  There are no
# constraints.  The search starts from a beam that is too deep: h = 20.
#
# THE KNOWN BEST CASE (beam theory, by calculus).  mass = a h  and  deflection = c / h^3  (a = rho b L, c = 4 P L^3 / (E b)).  The sum
# a h + k c / h^3 is smallest where its slope is zero:  h* = (3 k c / a)^(1/4).  k is chosen here so that h* = 12 mm; it is printed
# below with the answer.  The finite elements bend a little differently from the theory, so the optimizer's answer may be a little off.
#
# WHAT TO LOOK AT.  Three beams in a row: the starting beam (front, plain, 20 deep), the optimizer's answer (middle, coloured by stress)
# and the known best, 12 deep (back, plain).
import math
from fieldes import *

view.set_bounds([-5, -50, -5], [105, 60, 30], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

P, L, b, E = 200.0, 100.0, 20.0, aluminium.E
rho_kg = aluminium.density * 1000.0
a = rho_kg * b * L                                  # kg per mm of height
h_star = 12.0
c = a * h_star ** 4 / 3.0                           # chosen so that the slope is zero at 12 mm
k = c * E * b / (4.0 * P * L ** 3)                  # kg per mm of deflection
print('the measure is  mass + %.4f * tip deflection  (kg, kg per mm)' % k)
print('STARTING design : height 20')
print('KNOWN BEST      : height %.2f mm (beam theory)' % h_star)

height = var(20)
beam = box((0, 0, 0), (100, b, height))
wall = fixed(box((-1, -1, -1), (1, 60, 60)))
pull = force(box((99, -1, -1), (101, 60, 60)), (0, 0, -200))
stress = static_analysis(beam, supports=[wall], loads=[pull], material=aluminium, element_size=3)

best = parametric_optimization(stress, (height, 6, 24), minimize=lambda r: r.mass + k * r.max_displacement)

starting_beam = move(box((0, 0, 0), (100, b, 20)), (0, -35, 0))
known_best_beam = move(box((0, 0, 0), (100, b, h_star)), (0, 35, 0))
starting_beam
best
known_best_beam
