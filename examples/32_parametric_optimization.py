# Parametric optimization: the lightest beam that is stiff and strong enough, found by solving it again with other numbers.
#
# A cantilever, held at x = 0 and pulled down at its end with 200 N.  Its width and its height are var()s (the numbers you can drag), and
# parametric_optimization looks for the pair that gives the smallest mass while the beam stays under 90 MPa and bends less than 0.5 mm.
#
#   parametric_optimization(analysis, variables, conditions=[minimize(...), ...])
#
#   analysis      the result of an analysis made of the var()s: here `stress`.  Any analysis will do -- static, modal, thermal, fluid
#   variables     (var, low, high): the number that is changed and the range it is looked for in
#   conditions    what the design is for, one list of optimization conditions made from results of the analysis.  The goal: minimize(x) makes x as small as it can be,
#                 maximize(x) as large -- x a result (mass_1, max_von_mises_1, safety_factor_1, frequency_1 ...) or a formula of them.  And what the best design keeps:
#                 keep_below(max_von_mises_1, 90), keep_between(frequency_1, 100, 100) for an exact value, keep_between(frequency_1, 80, 120)
#   apply         True writes the best numbers into the var()s of this text when the run is over (otherwise they hold for this run)
#
# It needs no derivatives: it solves a spread of designs, learns how the numbers move with the variables (and where each limit is), and
# solves the design that should be best, again and again -- a few tens of solves, each one an ordinary analysis that is kept, so running
# the script again finds them all in the cache.  The result is the analysis of the best design (look at the stress on the beam, the result
# card), and the output pane lists every design that was tried, with the ones that broke a limit marked.
#
# Things to try: raise the allowed stress to 150 and the beam gets thinner; ask for keep_below(max_displacement_1, 0.2) and the displacement limit
# decides instead; maximize(safety_factor_1) with keep_below(mass_1, 0.05) asks for the strongest beam of a given weight.
from fieldes import *

view.set_bounds([-5, -5, -5], [105, 35, 30], auto_scale=True)
view.set_resolution(15)
view.set_quality(8)

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
best
