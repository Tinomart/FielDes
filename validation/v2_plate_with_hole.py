# Verification 2: the stress concentration at a hole in a plate in tension.
#
# A flat plate 80 mm long, 40 mm wide and 3 mm thick, with a round hole of 8 mm in its middle, is pulled along its length with a stress of 100 MPa over the whole width.
# The stress at the edge of the hole, across the plate, is higher than the 100 MPa that is pulled with: by the factor that Peterson's charts give for a hole in a
# plate of finite width (d = hole diameter, W = width), on the stress over the net section:
#
#     Kt_net = 3.00 - 3.13 d/W + 3.66 (d/W)^2 - 1.53 (d/W)^3          and over the whole width   Kt = Kt_net / (1 - d/W) = 3.135 here
#
# The script solves the same problem with static_analysis and reads the stress along the line across the plate that leaves the hole at its widest.  ONE EIGHTH of the plate is
# solved: the plate is symmetric about its middle plane and about the two planes through the middle of the hole, so the planes of symmetry are held in the direction across
# them (x = 0, y = 0 and z = 0) and nothing else is held.  That is the same problem with eight times the elements for the same cost, and it is what a stress concentration
# needs: the elements at the edge of the hole must be small, and there must be several layers across the thickness.  (The whole plate, as a thin sheet with
# the elements it can afford, gave a result that did not get steadily nearer as the elements got smaller.)
#
# A tetrahedron with four nodes has the same stress all over it, so the peak at the edge of the hole is low, and it comes nearer to Peterson's number only as the elements
# get smaller (first order: 1 mm elements -9 %, 0.5 mm -8 %, 0.25 mm -4 %); that is what the tolerance is for, and why this check is worth running after a change to the
# elements.  It does not use the result cache, and it takes about 20 seconds (1,000,000 elements).
# WHAT YOU SEE when you open it: one eighth of the plate coloured by the stress (the 10-node result), with the three planes of symmetry and the pull drawn on it.  The model tree has a row for each
# analysis (`result`, linear tetrahedra; `quad`, 10-node ones) and the conditions are in the row "boundary conditions" of each.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, W, T, D = 80.0, 40.0, 3.0, 8.0     # mm: the plate and the hole
SIGMA = 100.0                         # MPa, over the whole width
SIZE = 0.25                           # mm: the elements
TOLERANCE = 0.06                      # 6 %: linear tetrahedra (the program gets about 4 % here)
SIZE_10 = 0.7                         # mm: the 10-node elements (a mesh too coarse for the hole -- 1 mm, four to its radius -- reads the peak 20 % too high: halve the size and see whether it moves)
TOLERANCE_10 = 0.03                   # 3 %: 10-node tetrahedra (the program gets less than 1 % here)

view.set_bounds((-5, -5, -5), (L / 2 + 5, W / 2 + 5, T / 2 + 5), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of a model for every unit, the result as every model: 100 across
view.set_quality(8)

# one eighth: the hole is at the origin, x = 0 .. L/2 (along the pull), y = 0 .. W/2 (across), z = 0 .. T/2
a, b, h = L / 2, W / 2, T / 2
part = difference(box((a, b, h), position=(a / 2, b / 2, h / 2)), cylinder(D / 2, h + 2, position=(0, 0, h / 2)))     # (the hole goes through the plate: a model is placed by its middle, so the cylinder of height h + 2 is centred on the plate's middle h / 2)
plane_x = box((1.01, (b + 1) - (-1), (h + 1) - (-1)), position=(-0.495, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))                                 # the plane of symmetry x = 0: held in x only
hold_x = fixed(plane_x, displacement=(0, inf, inf))
plane_y = box(((a + 1) - (-1), 1.01, (h + 1) - (-1)), position=(((-1) + (a + 1)) / 2, -0.495, ((-1) + (h + 1)) / 2))                                 # the plane of symmetry y = 0: held in y only
hold_y = fixed(plane_y, displacement=(inf, 0, inf))
plane_z = box(((a + 1) - (-1), (b + 1) - (-1), 1.01), position=(((-1) + (a + 1)) / 2, ((-1) + (b + 1)) / 2, -0.495))                                 # the middle plane of the thickness, z = 0: held in z only
hold_z = fixed(plane_z, displacement=(inf, inf, 0))
pull = box(((a + 1) - (a - 0.01), (b + 1) - (-1), (h + 1) - (-1)), position=(((a - 0.01) + (a + 1)) / 2, ((-1) + (b + 1)) / 2, ((-1) + (h + 1)) / 2))                             # the pulled end
load = force(pull, (SIGMA * b * h, 0, 0))                            # the pull of 100 MPa over the cross-section of this eighth
result = static_analysis(part, supports=[hold_x, hold_y, hold_z], loads=[load], material=steel, element_size=SIZE, cache=False)
quad = static_analysis(part, supports=[hold_x, hold_y, hold_z], loads=[load], material=steel, element_size=SIZE_10, element='tet10', cache=False)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the analysis and the conditions it was solved with (the eye of each row in the model tree shows or hides it)
quad                                  # one eighth of the plate, coloured by the stress (the 10-node result)
hold_x                                # the plane of symmetry x = 0, held across itself
hold_y                                # the plane of symmetry y = 0
hold_z                                # the middle plane of the thickness, z = 0
load                                  # the pull on the end

r = D / W
theory = (3.00 - 3.13 * r + 3.66 * r ** 2 - 1.53 * r ** 3) / (1.0 - r)
# the line across the plate on the plane x = 0, from the edge of the hole outwards: the stress is highest at the edge and falls to 100 MPa
peak = max(result.von_mises(0.02, D / 2 + s / 40.0, h * 0.5) for s in range(0, 41))
error = peak / SIGMA / theory - 1.0
print("stress concentration: static_analysis %.2f, Peterson %.2f: %+.1f %%  (peak %.0f MPa, %d elements)" % (peak / SIGMA, theory, 100 * error, peak, result.elements))
assert abs(error) <= TOLERANCE, "the stress concentration is %.1f %% from Peterson's: more than %.0f %%" % (100 * error, 100 * TOLERANCE)
print("ok: within %.0f %%" % (100 * TOLERANCE))

peak10 = max(quad.von_mises(0.02, D / 2 + s / 40.0, h * 0.5) for s in range(0, 41))
error10 = peak10 / SIGMA / theory - 1.0
print("with 10-node elements:  static_analysis %.2f, Peterson %.2f: %+.1f %%  (peak %.0f MPa, %d elements)" % (peak10 / SIGMA, theory, 100 * error10, peak10, quad.elements))
assert abs(error10) <= TOLERANCE_10, "the stress concentration with 10-node elements is %.1f %% from Peterson's: more than %.0f %%" % (100 * error10, 100 * TOLERANCE_10)
print("ok: within %.0f %%" % (100 * TOLERANCE_10))
