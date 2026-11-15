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
# WHAT YOU SEE when you open it: the water in the channel coloured by its speed with its streamlines; the inlet, the outlet and the frictionless faces are the conditions in the row "boundary conditions" of the flow.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *

L, H, T = 40.0, 10.0, 4.0            # mm: the channel and the thickness of the slab
U = 5.0                              # mm/s: the mean speed at the inlet (Re = U H / nu = 50 in water)
MU = 1.0e-9                          # MPa s: water
TOLERANCE_SPEED, TOLERANCE_GRADIENT = 0.03, 0.06

view.set_bounds((-4, -4, -4), (L + 4, H + 4, T + 4), auto_scale=True)
view.set_resolution(10)                # ten samples across the longest side of a model for every unit, the result as every model: 100 across
view.set_quality(8)

channel = box((L, H, T), position=(L / 2, H / 2, T / 2))                                        # the domain: the water
nothing = sphere(0.1, position=(-100, -100, -100))                                  # (the body the flow goes around: there is none, so one far away)
inlet_face = box((1.01, (H + 1) - (-1), (T + 1) - (-1)), position=(-0.495, ((-1) + (H + 1)) / 2, ((-1) + (T + 1)) / 2))
inflow = inlet(inlet_face, speed=U, profile='developed')
outlet_face = box(((L + 1) - (L - 0.01), (H + 1) - (-1), (T + 1) - (-1)), position=(((L - 0.01) + (L + 1)) / 2, ((-1) + (H + 1)) / 2, ((-1) + (T + 1)) / 2))
outflow = outlet(outlet_face, pressure=0)
faces = union(box(((L + 1) - (-1), (H + 1) - (-1), 1.01), position=(((-1) + (L + 1)) / 2, ((-1) + (H + 1)) / 2, -0.495)), box(((L + 1) - (-1), (H + 1) - (-1), (T + 1) - (T - 0.01)), position=(((-1) + (L + 1)) / 2, ((-1) + (H + 1)) / 2, ((T - 0.01) + (T + 1)) / 2)))     # a frictionless face on each side: a two-dimensional flow
sides = slip(faces)                                                  # (the walls at y = 0 and y = H are in no region: walls at rest)
flow = fluid_analysis(nothing, channel, inlets=[inflow], outlets=[outflow], boundaries=[sides], fluid=water, element_size=1.0, cache=False)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the analysis (the eye of its row in the model tree shows or hides it).  The conditions it was solved with (`inflow`: the developed profile at 5 mm/s,
# `outflow`: pressure 0, `sides`: the frictionless faces that make the flow two-dimensional) are rows of the model tree with their eyes off, because their arrows and discs cover the fluid: switch one on there
flow                                 # the fluid, coloured by the speed, with its streamlines

peak = flow.vx(L / 2, H / 2, T / 2)
gradient = (flow.pressure(10.0, H / 2, T / 2) - flow.pressure(30.0, H / 2, T / 2)) / 20.0
exact_peak, exact_gradient = 1.5 * U, 12.0 * MU * U / H ** 2
error_peak, error_gradient = peak / exact_peak - 1.0, gradient / exact_gradient - 1.0
print("speed at the middle:  fluid_analysis %.3f mm/s, exact %.3f mm/s: %+.1f %%" % (peak, exact_peak, 100 * error_peak))
print("pressure gradient:    fluid_analysis %.3g MPa/mm, exact %.3g MPa/mm: %+.1f %%  (%d elements)" % (gradient, exact_gradient, 100 * error_gradient, flow.elements))
assert abs(error_peak) <= TOLERANCE_SPEED, "the speed at the middle is %.1f %% from the exact solution: more than %.0f %%" % (100 * error_peak, 100 * TOLERANCE_SPEED)
assert abs(error_gradient) <= TOLERANCE_GRADIENT, "the pressure gradient is %.1f %% from the exact solution: more than %.0f %%" % (100 * error_gradient, 100 * TOLERANCE_GRADIENT)
print("ok")
