# Topology optimization against the one layout whose best design is known exactly: a bar in tension.
#
# THE PROBLEM.  A bar of aluminium 40 mm long and 10 x 10 mm in section, held across one end and pulled along its length across the other with 1000 N.  Find the stiffest layout that uses 50 % of the bar.
#
# THE EXACT ANSWER (derived here, no outside source claimed).  Every cross-section of a bar in tension carries the whole force P, and the compliance (the work of the load, P times the end's movement) is the
# integral of P^2 / (E A(x)) along the bar, A(x) the area of material across it.  For a given total volume V = A_mean L that integral is least when A(x) is the same everywhere (Jensen's inequality; any other
# distribution of the same volume is stiffer nowhere).  So the stiffest bar of volume fraction f has the compliance
#
#     C(f) = P^2 L / (E A f),       and for the whole bar (f = 1)  C(1) = P^2 L / (E A).
#
# No layout of the material can beat C(f): this is a lower bound, not only the answer of one method.  A topology optimization has to land on it, and not past it.  WHAT THE SCRIPT DOES: solves the whole bar and
# checks C(1) (a static analysis, to 5 %: the support holds the whole end face in every direction, so the end cannot draw in sideways and the bar is a few percent stiffer than the formula, which has no ends);
# runs topology_optimization for 50 %; reads the volume fraction the cut part really has and the compliance of that part's own analysis; and stops with an AssertionError when the part is stiffer than the bound by
# more than 3 % (a design cannot beat the bound: that would be an error of the program) or more than 30 % less stiff (the optimizer must find a layout that is nearly uniform along the bar).  THE BOUND USED IS
# THE WHOLE BAR'S OWN COMPLIANCE DIVIDED BY THE VOLUME FRACTION, C_whole / f, the same model with the same ends (the exact C(f) of the formula is printed beside it): the effect of the ends then cancels and what
# is left is the one question, whether the optimizer's layout is as good as a uniform bar of the same volume.  The tolerance says how far a mesh of 2 mm elements and a density that is not crisp everywhere are
# from the continuum.  About a minute.
from fieldes import *

L, b, h = 40.0, 10.0, 10.0           # mm: the bar
P, FRACTION = 1000.0, 0.5            # N, the share of the bar the design may use
E = aluminium.E                      # MPa = N/mm^2
A = b * h

bar = box((L, b, h), position=(L / 2, b / 2, h / 2))
wall = box((2, b + 2, h + 2), position=(0, b / 2, h / 2))
pull = box((2, b + 2, h + 2), position=(L, b / 2, h / 2))
stress = static_analysis(bar, supports=[fixed(wall)], loads=[force(pull, (P, 0, 0))], material=aluminium, element_size=2.0, cache=False)
c_whole_exact = P * P * L / (E * A)
c_whole = float(stress.compliance)
print('the whole bar: compliance %.5f N mm, exact P^2 L / (E A) = %.5f (%+.2f %%)' % (c_whole, c_whole_exact, 100 * (c_whole / c_whole_exact - 1)))
if abs(c_whole / c_whole_exact - 1) > 0.05:
    raise AssertionError('the compliance of the whole bar is %.1f %% from P^2 L / (E A)' % (100 * (c_whole / c_whole_exact - 1)))

compliance_1 = stress.compliance
volume_fraction_1 = stress.volume_fraction
best = topology_optimization(stress, conditions=[minimize(compliance_1), keep_below(volume_fraction_1, FRACTION)], iterations=40, cache=False)
part = best.analysis
f_part = float(part.volume) / float(stress.volume)          # the share of the bar that the cut part really has: the volumes of the two meshes
c_part = float(part.compliance)
c_bound = c_whole / f_part
print('the optimized part keeps %.1f %% of the bar (asked %.0f %%); its compliance %.5f N mm; the best that volume can do, the whole bar\'s %.5f / f = %.5f (%+.1f %%); the formula P^2 L / (E A f) gives %.5f' % (
    100 * f_part, 100 * FRACTION, c_part, c_whole, c_bound, 100 * (c_part / c_bound - 1), c_whole_exact / f_part))
if c_part < 0.97 * c_bound:
    raise AssertionError('the part is %.1f %% stiffer than the best layout that exists: more than the 3 %% of the tolerance' % (100 * (1 - c_part / c_bound)))
if c_part > 1.30 * c_bound:
    raise AssertionError('the part is %.1f %% less stiff than the best layout: more than 30 %%' % (100 * (c_part / c_bound - 1)))
print('OK')
