# Verification 9: the lid-driven square cavity at a Reynolds number of 100 (laminar flow) against the benchmark of Ghia, Ghia and Shin, the set-up of an open-source verification manual.
#
# The problem is "FLD03: Steady, lid-driven square cavity" of the verification and validation manual of the open-source program MFiX of the US Department of Energy
# (https://mfix.netl.doe.gov/doc/vvuq-manual/main/html/fluid/fld-03.html), which follows U. Ghia, K. N. Ghia and C. T. Shin, "High-Re solutions for incompressible flow using the Navier-Stokes equations and a
# multigrid method", Journal of Computational Physics 48 (1982) 387-411: a square cavity filled with an incompressible fluid, three walls at rest and the fourth, the lid, moving along itself at a constant speed;
# the Reynolds number is the lid's speed times the side over the fluid's kinematic viscosity, 100 here.  Here the cavity is 10 mm on a side, the lid moves at 10 mm/s and the fluid is water (Re = 10 x 10 = 100, in the units
# of the program); the cavity is a slab with a frictionless face on each side, so the flow is the two-dimensional one.
#
# The reference values are those of Ghia et al. for Re = 100 -- the horizontal speed u / (lid speed) on the vertical line through the middle of the cavity and the vertical speed v / (lid speed) on the horizontal
# line through it, 17 points each -- as printed on https://sites.google.com/view/sidrg/research-projects/mechanical-engineering/lid-driven-cavity-flow (a transcription of the paper's table: the original paper
# was not opened for this script).  The script solves the flow with fluid_analysis and prints the speeds at those points beside the reference, and stops with an error when one is farther off than the tolerance.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

H, T = 10.0, 2.0                      # mm: the side of the cavity and the thickness of the slab
U = 10.0                              # mm/s: the speed of the lid (Re = U H / nu = 100 for water, nu = 1 mm^2/s)
TOLERANCE = 0.04                      # of the lid's speed: linear tetrahedra with stabilisation read the peaks of the profiles low on a coarse mesh (28 cells across here) and come nearer as it is refined

Y = [1.0000, 0.9766, 0.9688, 0.9609, 0.9531, 0.8516, 0.7344, 0.6172, 0.5000, 0.4531, 0.2813, 0.1719, 0.1016, 0.0703, 0.0625, 0.0547, 0.0000]
U_100 = [1.0000, 0.8412, 0.7887, 0.7372, 0.68717, 0.2315, 0.0033, -0.1364, -0.2058, -0.2109, -0.1566, -0.1015, -0.0643, -0.04775, -0.0419, -0.0371, 0.0000]
X = [1.0000, 0.9688, 0.9609, 0.9531, 0.9453, 0.9063, 0.8594, 0.8047, 0.5000, 0.2344, 0.2266, 0.1563, 0.0938, 0.0781, 0.0703, 0.0625, 0.0000]
V_100 = [0.0000, -0.05906, -0.0739, -0.0886, -0.10313, -0.16914, -0.22445, -0.24533, 0.05454, 0.17527, 0.17507, 0.16077, 0.12317, 0.1089, 0.1009, 0.0923, 0.0000]

view.set_bounds((-2, -2, -2), (H + 2, H + 2, T + 2), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

cavity = box((H, H, T), position=(H / 2, H / 2, T / 2))                                          # the domain: the water
nothing = sphere(0.1, position=(-100, -100, -100))                                                # (the body the flow goes around: there is none, so one far away)
faces = union(box(((H + 1) - (-1), (H + 1) - (-1), 1.01), position=(((-1) + (H + 1)) / 2, ((-1) + (H + 1)) / 2, -0.495)),
              box(((H + 1) - (-1), (H + 1) - (-1), (T + 1) - (T - 0.01)), position=(((-1) + (H + 1)) / 2, ((-1) + (H + 1)) / 2, ((T - 0.01) + (T + 1)) / 2)))     # a frictionless face on each side
lid = box(((H + 1) - (-1), 1.01, (T + 1) - (-1)), position=(((-1) + (H + 1)) / 2, H + 0.495, ((-1) + (T + 1)) / 2))                  # the top of the cavity, y = H
flow = fluid_analysis(nothing, cavity, inlets=[], outlets=[], boundaries=[wall(lid, velocity=(U, 0, 0)), slip(faces)], fluid=water, element_size=0.35, cache=False)

worst = 0.0
print("u / U along the vertical line x = H/2:   y / H    Ghia et al.   fluid_analysis")
for y, ref in zip(Y, U_100):
    u = float(flow.vx(H / 2, min(max(y * H, 0.05), H - 0.05), T / 2)) / U
    worst = max(worst, abs(u - ref))
    print("                                      %6.4f    %9.5f     %9.5f" % (y, ref, u))
print("v / U along the horizontal line y = H/2:  x / H    Ghia et al.   fluid_analysis")
for x, ref in zip(X, V_100):
    v = float(flow.vy(min(max(x * H, 0.05), H - 0.05), H / 2, T / 2)) / U
    worst = max(worst, abs(v - ref))
    print("                                      %6.4f    %9.5f     %9.5f" % (x, ref, v))
print("worst difference: %.4f of the lid's speed  (%s elements)" % (worst, getattr(flow, 'elements', '?')))
if worst > TOLERANCE:
    raise AssertionError("a speed is %.3f of the lid's speed from the benchmark: more than %.2f" % (worst, TOLERANCE))
print("ok")

flow                                  # shown: the speed on the fluid
