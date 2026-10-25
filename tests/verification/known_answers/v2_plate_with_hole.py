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
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, W, T, D = 80.0, 40.0, 3.0, 8.0     # mm: the plate and the hole
SIGMA = 100.0                         # MPa, over the whole width
SIZE = 0.25                           # mm: the elements
TOLERANCE = 0.06                      # 6 %: linear tetrahedra (the program gets about 4 % here)
SIZE_10 = 0.7                         # mm: the 10-node elements (see the end)
TOLERANCE_10 = 0.03                   # 3 %: 10-node tetrahedra (the program gets less than 1 % here)

view.set_bounds((-5, -5, -5), (L / 2 + 5, W / 2 + 5, T / 2 + 5), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

# one eighth: the hole is at the origin, x = 0 .. L/2 (along the pull), y = 0 .. W/2 (across), z = 0 .. T/2
a, b, h = L / 2, W / 2, T / 2
part = difference(box((0, 0, 0), (a, b, h)), cylinder(D / 2, h + 2, (0, 0, -1)))
plane_x = box((-1, -1, -1), (0.01, b + 1, h + 1))                                 # the plane of symmetry x = 0: held in x only
plane_y = box((-1, -1, -1), (a + 1, 0.01, h + 1))                                 # the plane of symmetry y = 0: held in y only
plane_z = box((-1, -1, -1), (a + 1, b + 1, 0.01))                                 # the middle plane of the thickness, z = 0: held in z only
pull = box((a - 0.01, -1, -1), (a + 1, b + 1, h + 1))                             # the pulled end
result = static_analysis(part, supports=[fixed(plane_x, displacement=(0, inf, inf)), fixed(plane_y, displacement=(inf, 0, inf)), fixed(plane_z, displacement=(inf, inf, 0))],
                         loads=[force(pull, (SIGMA * b * h, 0, 0))], material=steel, element_size=SIZE, cache=False)

r = D / W
theory = (3.00 - 3.13 * r + 3.66 * r ** 2 - 1.53 * r ** 3) / (1.0 - r)
# the line across the plate on the plane x = 0, from the edge of the hole outwards: the stress is highest at the edge and falls to 100 MPa
peak = max(result.von_mises(0.02, D / 2 + s / 40.0, h * 0.5) for s in range(0, 41))
computed = peak / SIGMA
error = computed / theory - 1.0
print("stress concentration: static_analysis %.2f, Peterson %.2f: %+.1f %%  (peak %.0f MPa, %d elements)" % (computed, theory, 100 * error, peak, result.elements))
if abs(error) > TOLERANCE:
    raise AssertionError("the stress concentration is %.1f %% from Peterson's: more than %.0f %%" % (100 * error, 100 * TOLERANCE))
print("ok: within %.0f %%" % (100 * TOLERANCE))

# The same plate with 10-node elements (element='tet10'): elements of 0.7 mm read the peak better than linear ones of 0.25 mm (a mesh too coarse for the
# hole -- 1 mm, four to its radius -- reads it 20 % too high: halve the size and see whether the peak moves)
quad = static_analysis(part, supports=[fixed(plane_x, displacement=(0, inf, inf)), fixed(plane_y, displacement=(inf, 0, inf)), fixed(plane_z, displacement=(inf, inf, 0))],
                       loads=[force(pull, (SIGMA * b * h, 0, 0))], material=steel, element_size=SIZE_10, element='tet10', cache=False)
peak10 = max(quad.von_mises(0.02, D / 2 + s / 40.0, h * 0.5) for s in range(0, 41))
error10 = peak10 / SIGMA / theory - 1.0
print("with 10-node elements:  static_analysis %.2f, Peterson %.2f: %+.1f %%  (peak %.0f MPa, %d elements)" % (peak10 / SIGMA, theory, 100 * error10, peak10, quad.elements))
if abs(error10) > TOLERANCE_10:
    raise AssertionError("the stress concentration with 10-node elements is %.1f %% from Peterson's: more than %.0f %%" % (100 * error10, 100 * TOLERANCE_10))
print("ok: within %.0f %%" % (100 * TOLERANCE_10))

quad                                  # shown: one eighth of the plate, coloured by the stress (the 10-node result)
