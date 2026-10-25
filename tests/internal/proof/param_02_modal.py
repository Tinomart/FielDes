# Parametric optimization 2: does it find the known best section of a vibrating bar?
#
# THE PROBLEM.  A bar held at one end, b x h in section, 100 mm long.  Its mass may not pass 0.04 kg.  Make its first natural frequency as
# high as possible (maximize frequencies[0] of a modal analysis).  The search starts from a flat section, 8 x 16 mm, which is stiff the
# one way and weak the other.
#
# THE KNOWN BEST CASE.  A bar vibrates first the weak way, and the frequency of a cantilever is  f = 0.5596 sqrt(E / (12 rho)) * thickness / L^2
# (Euler-Bernoulli) where `thickness` is the smaller of b and h.  At a fixed cross-section area b h the smaller side is largest when the
# section is a SQUARE.  So the best section for the mass is square: b = h = sqrt(mass / (rho L)), printed below (about 12.2 mm), a frequency
# a little under 1000 Hz (the finite elements read a little higher than the theory).
#
# WHAT TO LOOK AT.  Three bars in a row: the starting bar (front, plain), the optimizer's answer (middle, coloured by its first mode) and the
# known best square bar (back, plain).  Every design is a real modal solve (the results are cached like every analysis: running the script again reads them back; to solve everything again, delete the result cache, see README.txt).
import math
from fieldes import *

view.set_bounds([-5, -50, -5], [105, 60, 30], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

E, rho, L, mass_limit = aluminium.E, aluminium.density, 100.0, 0.04
rho_kg = rho * 1000.0
side = math.sqrt(mass_limit / (rho_kg * L))
def euler_bernoulli(thickness):
    return 0.5596 * math.sqrt(E / (12.0 * rho)) * thickness / L ** 2
print('STARTING bar 8 x 16   : weak side 8     -> first frequency by theory %.0f Hz, mass %.4f kg' % (euler_bernoulli(8), rho_kg * 8 * 16 * L))
print('KNOWN BEST %.2f square : weak side %.2f -> first frequency by theory %.0f Hz, mass %.4f kg' % (side, side, euler_bernoulli(side), mass_limit))

b = var(8)
h = var(16)
bar = box((0, 0, 0), (100, b, h))
wall = fixed(box((-1, -1, -1), (1, 40, 40)))
modes = modal_analysis(bar, supports=[wall], material=aluminium, modes=3, element_size=3)

best = parametric_optimization(modes, [(b, 4, 20), (h, 4, 20)],
                               constraints=[maximize('frequencies[0]'), 'mass <= 0.04'])

starting_bar = move(box((0, 0, 0), (100, 8, 16)), (0, -35, 0))
known_best_bar = move(box((0, 0, 0), (100, side, side)), (0, 30, 0))
starting_bar
best
known_best_bar
