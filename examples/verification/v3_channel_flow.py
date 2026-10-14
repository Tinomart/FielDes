# Verification 3: the flow between two plates (Poiseuille flow) against its exact solution.
#
# Water flows through a channel 10 mm high and 40 mm long, between two walls; the channel is a thin slab with a frictionless face on each side, so the flow is the
# two-dimensional one between plates.  It comes in with the developed profile at a mean speed of 5 mm/s, and the exact solution of the Navier-Stokes equations is known:
#
#     the speed at the middle of the channel  = 1.5 x the mean speed
#     the pressure falls along it by           dp/dx = - 12 mu U / H^2        (mu = 1e-9 MPa s for water, U the mean speed, H the height)
#
# The script solves the flow with fluid_analysis (the steady Navier-Stokes equations on linear tetrahedra, 10 elements across the channel) and prints both numbers for each.
# It stops with an error when one is farther from the exact solution than its tolerance.  The pressure is read between x = 10 and x = 30 mm, away from the ends.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, H, T = 40.0, 10.0, 4.0            # mm: the channel and the thickness of the slab
U = 5.0                              # mm/s: the mean speed at the inlet (Re = U H / nu = 50 in water)
MU = 1.0e-9                          # MPa s: water
TOLERANCE_SPEED, TOLERANCE_GRADIENT = 0.03, 0.06

view.set_bounds((-4, -4, -4), (L + 4, H + 4, T + 4), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

channel = box((0, 0, 0), (L, H, T))                                        # the domain: the water
nothing = sphere(0.1, (-100, -100, -100))                                  # (the body the flow goes around: there is none, so one far away)
faces = union(box((-1, -1, -1), (L + 1, H + 1, 0.01)), box((-1, -1, T - 0.01), (L + 1, H + 1, T + 1)))     # a frictionless face on each side: a two-dimensional flow
flow = fluid_analysis(nothing, channel,
                      inlets=[inlet(box((-1, -1, -1), (0.01, H + 1, T + 1)), speed=U, profile='developed')],
                      outlets=[outlet(box((L - 0.01, -1, -1), (L + 1, H + 1, T + 1)), pressure=0)],
                      boundaries=[slip(faces)],                            # (the walls at y = 0 and y = H are in no region: walls at rest)
                      fluid=water, element_size=1.0, cache=False)

peak = flow.vx(L / 2, H / 2, T / 2)
gradient = (flow.pressure(10.0, H / 2, T / 2) - flow.pressure(30.0, H / 2, T / 2)) / 20.0
exact_peak, exact_gradient = 1.5 * U, 12.0 * MU * U / H ** 2
error_peak, error_gradient = peak / exact_peak - 1.0, gradient / exact_gradient - 1.0
print("speed at the middle:  fluid_analysis %.3f mm/s, exact %.3f mm/s: %+.1f %%" % (peak, exact_peak, 100 * error_peak))
print("pressure gradient:    fluid_analysis %.3g MPa/mm, exact %.3g MPa/mm: %+.1f %%  (%d elements)" % (gradient, exact_gradient, 100 * error_gradient, flow.elements))
if abs(error_peak) > TOLERANCE_SPEED:
    raise AssertionError("the speed at the middle is %.1f %% from the exact solution: more than %.0f %%" % (100 * error_peak, 100 * TOLERANCE_SPEED))
if abs(error_gradient) > TOLERANCE_GRADIENT:
    raise AssertionError("the pressure gradient is %.1f %% from the exact solution: more than %.0f %%" % (100 * error_gradient, 100 * TOLERANCE_GRADIENT))
print("ok")

flow                                 # shown: the speed on the fluid
