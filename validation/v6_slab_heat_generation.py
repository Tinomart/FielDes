# Verification 6: heat generated inside a slab (one-dimensional conduction with a source) against its exact solution.
#
# An aluminium bar 100 mm long and 10 x 10 mm in section is held at 20 C at both ends and nothing leaves its sides.  A power of 20 W is generated evenly through its volume.  The temperature along the bar is then a
# parabola (the solution of  k T'' = -q,  q the power per volume), highest in the middle:
#
#     T(x) = 20 + q x (L - x) / (2 k)        q = 20 W / (100 x 10 x 10 mm^3) = 0.002 W/mm^3,   k = 0.167 W/(mm K) for aluminium,   T(L/2) = 20 + q L^2 / (8 k) = 35.0 C
#
# (integrate k T'' = -q twice with T = 20 at both ends; this derivation is the only source claimed).  The script solves it with thermal_analysis and prints the temperature
# at three places against the parabola.  Linear tetrahedra represent a parabola only approximately, so the tolerance is wider than for a straight line: it stops with an error when a temperature is farther off than
# 2 % of the rise of the middle above the ends.
# WHAT YOU SEE when you open it: the bar coloured by its temperature (hottest in the middle), with the two cold ends and the generated heat drawn on it (the conditions are in the row "boundary conditions" of `result`).
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, b, h = 100.0, 10.0, 10.0          # mm: the bar
T_ENDS, WATTS = 20.0, 20.0           # C at both ends, W generated in the bar
TOLERANCE = 0.02                     # of the rise of the middle above the ends

view.set_bounds((-10, -10, -10), (L + 10, b + 10, h + 10), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of a model for every unit, the result as every model: 100 across
view.set_quality(8)

bar = box((L, b, h), position=(L / 2, b / 2, h / 2))
end_0 = box((1.01, (b + 1) - (-1), (h + 1) - (-1)), position=(-0.495, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
cold_0 = fixed_temperature(end_0, T_ENDS)                        # one end held at 20 C
end_1 = box(((L + 1) - (L - 0.01), (b + 1) - (-1), (h + 1) - (-1)), position=(((L - 0.01) + (L + 1)) / 2, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))
cold_1 = fixed_temperature(end_1, T_ENDS)                        # the other end held at 20 C
source = heat_generation(bar, watts=WATTS)                       # the 20 W generated through the volume
result = thermal_analysis(bar, fixed_temperatures=[cold_0, cold_1], heat_inputs=[], heat_generations=[source], convections=[], material=aluminium, element_size=3.0, cache=False)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the analysis and the conditions it was solved with (the eye of each row in the model tree shows or hides it)
result                               # the bar, coloured by the temperature
cold_0                               # one end held at 20 C
cold_1                               # the other end held at 20 C
source                               # the 20 W generated through the volume

k = aluminium.conductivity                                       # W/(mm K)
q = WATTS / (L * b * h)                                          # W/mm^3
rise = q * L ** 2 / (8.0 * k)
places = (25.0, 50.0, 75.0)
exact = [T_ENDS + q * x * (L - x) / (2.0 * k) for x in places]
temperatures = [float(result.temperature(x, b / 2, h / 2)) for x in places]
print('\n'.join("x = %2.0f mm: T thermal_analysis %.2f C, exact %.2f C" % (x, t, e) for x, t, e in zip(places, temperatures, exact)))
worst = max(abs(t - e) / rise for t, e in zip(temperatures, exact))
print("worst: %.1f %% of the rise of the middle (%.1f K)  (%s elements)" % (100 * worst, rise, getattr(result, 'elements', '?')))
assert worst <= TOLERANCE, "a temperature is %.1f %% of the rise from the parabola: more than %.0f %%" % (100 * worst, 100 * TOLERANCE)
print("ok")
