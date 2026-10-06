# Fields in the analyses: a material whose stiffness varies, a load that is not the same everywhere.
#
# Material(E=...), density, conductivity and expansion can each be a field -- a Shape: its value at every point of the part
# is the property there.  A load takes a profile (a field) that spreads its total over the surface in proportion to it, and
# a heat condition takes fields too (the temperature held, the convection's coefficient and ambient temperature, a profile
# of a heat input).  They work with the tetrahedral elements, the default.
#
# A cantilever, 100 mm long, held at x = 0, carrying 200 N down:
#   1. all at the tip, in aluminium                         -- beam theory: P L^3 / (3 E I)
#   2. the same, in a material that is a tenth as stiff at the tip as at the wall (a field of x)
#   3. spread evenly along the top face                     -- beam theory: 3/8 of 1.
#   4. spread along the top face in proportion to x (a profile): none at the wall, most at the tip  -- 11/20 of 1.
# Each prints the deflection of the tip; the last is the result that is shown (the stress, coloured on the beam).
#
# No STEP file is needed.
from fieldes import *

view.set_bounds([-5, -5, -15], [105, 25, 15])
view.set_resolution(4)
view.set_quality(8)

beam = box_exact((0, 0, 0), (100, 20, 10))
wall = box_exact((-1, -1, -1), (0.01, 21, 11))
tip = box_exact((99.99, -1, -1), (101, 21, 11))
top = box_exact((-1, -1, 9.99), (101, 21, 11))
deflection = lambda r: -evaluate(r.uz, (100, 10, 5))


def analyse(loads, material=aluminium):
    conditions = static_boundary_conditions(beam, supports=[fixed(wall)], loads=loads)
    return static_analysis(beam, conditions, material=material, element_size=3)


# 1. the load at the tip
at_tip = analyse([force(tip, (0, 0, -200))])
print('1. at the tip:                        %.3f mm' % deflection(at_tip))

# 2. Young's modulus as a field of x: stiff at the wall, a tenth as stiff at the tip
graded = Material('graded aluminium', ramp(x_field(), (0, 100), (aluminium.E, aluminium.E / 10)), aluminium.nu)
softer = analyse([force(tip, (0, 0, -200))], graded)
print('2. stiffness falling to a tenth:      %.3f mm' % deflection(softer))

# 3. the same total spread evenly along the top
evenly = analyse([force(top, (0, 0, -200))])
print('3. evenly along the top:              %.3f mm  (%.2f of 1; beam theory 0.375)'
      % (deflection(evenly), deflection(evenly) / deflection(at_tip)))

# 4. ... and in proportion to x: a profile is a field that says how the total is shared
ramped = analyse([force(top, (0, 0, -200), profile=ramp(x_field(), (0, 100), (0.0, 1.0)))])
print('4. in proportion to x along the top:  %.3f mm  (%.2f of 1; beam theory 0.55)'
      % (deflection(ramped), deflection(ramped) / deflection(at_tip)))

# Results are fields too: this is what is drawn (the result card switches to the displacement)
ramped
