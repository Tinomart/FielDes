# Verification 2: the stress concentration at a hole in a plate in tension.
#
# A flat plate 80 mm long, 40 mm wide and 1 mm thick, with a round hole of 8 mm in its middle, is pulled along its length with a stress of 100 MPa over the whole width.
# The stress at the edge of the hole, across the plate, is higher than the 100 MPa that is pulled with: by the factor that Peterson's charts give for a hole in a
# plate of finite width (d = hole diameter, W = width), on the stress over the net section:
#
#     Kt_net = 3.00 - 3.13 d/W + 3.66 (d/W)^2 - 1.53 (d/W)^3          and over the whole width   Kt = Kt_net / (1 - d/W) = 3.135 here
#
# The script solves the same problem with static_analysis and reads the stress along the line across the plate that leaves the hole at its widest.  The stress of a
# tetrahedron is the same all over it, so the peak at the edge of the hole is a little low unless the elements there are small: that is what the tolerance is for (and why
# this check is worth running after a change to the elements).  The end of the plate is held only in the direction of the pull (it is free to contract), and one small
# place at its middle is held across it, so that nothing else is held.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, W, T, D = 80.0, 40.0, 1.0, 8.0     # mm: the plate and the hole
SIGMA = 100.0                         # MPa, over the whole width
TOLERANCE = 0.10                      # 10 %: linear tetrahedra, elements of 0.7 mm (the program gets about 5 % here)

view.set_bounds((-10, -10, -10), (L + 10, W + 10, T + 10), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

plate = difference(box((0, 0, 0), (L, W, T)), cylinder(D / 2, T + 2, (L / 2, W / 2, -1)))
end = box((-1, -1, -1), (0.01, W + 1, T + 1))                                     # the held end: held in x only (free to contract)
pin = box((-1, W / 2 - 1, -1), (1, W / 2 + 1, T + 1))                             # a small place at its middle: held across (no rigid movement is left)
pull = box((L - 0.01, -1, -1), (L + 1, W + 1, T + 1))                             # the pulled end
result = static_analysis(plate, supports=[fixed(end, y=False, z=False), fixed(pin, x=False)], loads=[force(pull, (SIGMA * W * T, 0, 0))],
                         material=steel, element_size=0.7, cache=False)

r = D / W
theory = (3.00 - 3.13 * r + 3.66 * r ** 2 - 1.53 * r ** 3) / (1.0 - r)
# the line across the plate at the middle of its length, from the edge of the hole outwards: the stress is highest at the edge and falls to 100 MPa
peak = max(result.von_mises(L / 2, W / 2 + D / 2 + s / 20.0, T / 2) for s in range(0, 21))
computed = peak / SIGMA
error = computed / theory - 1.0
print("stress concentration: static_analysis %.2f, Peterson %.2f: %+.1f %%  (peak %.0f MPa, %d elements)" % (computed, theory, 100 * error, peak, result.elements))
if abs(error) > TOLERANCE:
    raise AssertionError("the stress concentration is %.1f %% from Peterson's: more than %.0f %%" % (100 * error, 100 * TOLERANCE))
print("ok: within %.0f %%" % (100 * TOLERANCE))

result                                # shown: the plate, coloured by the stress
