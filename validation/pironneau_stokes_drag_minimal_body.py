# Verification 23: the flow topology optimization (`topology_optimization` of a `fluid_analysis`, example 17) in creeping flow against what is known of the best body.
#
# There is no exact number to compare with for a body in a channel, but there is something that is known about the answer and that an optimizer that is wrong would break: in Stokes flow the equations are reversible,
# so the drag of a body is the same when the flow runs the other way, and in a domain that is a mirror image of itself, with the place the body may be in a mirror image of itself, the BEST body is the same in front
# and behind.  Pironneau (J. Fluid Mech. 59, 1973, "On optimum profiles in Stokes flow") showed that the body of the lowest drag for a given volume in Stokes flow is front-back symmetric, with constant vorticity on its
# surface and pointed ends (the front and the rear taper into cones of 120 degrees included angle for a body of revolution; read in the abstract of Montenegro-Johnson and Lauga, J. Fluid Mech. 2015,
# arXiv 1411.4783).  The best body of a flow that has inertia is not symmetric (for a body of revolution at Reynolds numbers of 100 and more Glowinski and Pironneau found a wedge of 90 degrees at the front and a cusp at the rear, as quoted
# by Chen et al., arXiv 2009.14339), which is why the test is in creeping flow, `stokes=True`.
#
# The set-up is the slab of example 17 (80 x 40 x 4 mm, water, slip on the faces and the sides, a post of 10 mm that may be reshaped inside a box of 44 x 24 mm round it), but the post and the box are put in the MIDDLE of
# the slab and the optimizer is told not to keep the design symmetric (`symmetry=None`: its default would impose the symmetry across the flow, which is a test of nothing).  The optimizer minimizes the drag and keeps the
# post's volume.  The script then measures the final body in its middle plane and checks, with tolerances that say how loosely, that
#   - the body is the same in front and behind (overlap with its own mirror image over the length of the slab, at least 0.95 of the union), as Pironneau's body is;
#   - it is longer than it is wide (at least 1.4): the circle is not the best shape, a lens is;
#   - it keeps the post's volume (within 3 %);
#   - the force of the REAL flow around it (the body a wall, not the optimizer's friction model) is lower than the post's.
# The optimizer's own model of the drag (the body a friction in the flow) reads a much larger change than the real flow does, and the script prints both: that difference is the model's, not an error of the flow.
#
# What is NOT checked: the value of the lowest drag.  For a body in a channel it is not known in closed form, and the 120 degree tips are the result for a body of revolution in unbounded flow.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *
import math
from fieldes.stdlib.fields import evaluate

W, H, T = 80.0, 40.0, 4.0                  # mm: the slab of water
D = 10.0                                   # mm: the post
U = 4.0                                    # mm/s
SIZE = 1.5                                 # mm: the element size
OVERLAP_MIN, ELONGATION_MIN, VOLUME_TOL = 0.95, 1.4, 0.03

view.set_bounds((-4, -4, -4), (W + 4, H + 4, T + 4), auto_scale=True)
view.set_resolution(25)
view.set_quality(8)

post = cylinder(D / 2, T + 2, position=(W / 2, H / 2, -1.0 + (T + 2) / 2))
slab = box((W, H, T), position=(W / 2, H / 2, T / 2))
region = box((44, 24, T + 2), position=(W / 2, H / 2, T / 2))                                                   # where material may be: round the post, in the middle of the slab
outside = difference(box((W + 2, H + 2, T + 2), position=(W / 2, H / 2, T / 2)), region)                          # ... and everywhere else the fluid stays fluid
faces = union(box((W + 2, H + 2, 1.01), position=(W / 2, H / 2, -0.495)), box((W + 2, H + 2, 1.01), position=(W / 2, H / 2, T + 0.495)))
sides = union(box((W + 2, 1.01, T + 2), position=(W / 2, -0.495, T / 2)), box((W + 2, 1.01, T + 2), position=(W / 2, H + 0.495, T / 2)))
inlet_face = box((1.01, H + 2, T + 2), position=(-0.495, H / 2, T / 2))
outlet_face = box((1.01, H + 2, T + 2), position=(W + 0.495, H / 2, T / 2))

flow = fluid_analysis(post, slab, inlets=[inlet(inlet_face, speed=U)], outlets=[outlet(outlet_face, pressure=0)], boundaries=[slip(faces), slip(sides)],
                      fluid=water, element_size=SIZE, stokes=True, cache=False)
drag_force_1 = flow.drag_force
volume_fraction_1 = flow.volume_fraction
post_drag = float(flow.wall_force[0])
print("the post in creeping flow: drag %.4g N" % post_drag)
design = topology_optimization(flow, conditions=[minimize(drag_force_1), keep_between(volume_fraction_1, 1.0, 1.0)], avoid=outside, extrude='z', symmetry=None, cache=False)
print("optimizer: %d iterations; the optimizer's own model of the drag %.4g -> %.4g N (%+.0f %%); the real flow: the post %.4g N, the final body %.4g N (%+.1f %%)" % (
    design.iterations, design.drag[0], design.drag[-1], 100.0 * (design.drag[-1] / design.drag[0] - 1.0), post_drag, design.real_drag, 100.0 * (design.real_drag / post_drag - 1.0)))

# the final body in its middle plane, on a grid of half a millimetre, and its mirror images
shape = design.shape()
step = 0.5
nx, ny = int(W / step), int(H / step)
pts = [((i + 0.5) * step, (j + 0.5) * step, T / 2) for i in range(nx) for j in range(ny)]
vals = []
for s in range(0, len(pts), 20000):
    vals.extend(evaluate(shape, pts[s:s + 20000]))
inside = [v <= 0.0 for v in vals]
area = sum(inside) * step * step
volume = area * T
both_x = sum(1 for i in range(nx) for j in range(ny) if inside[i * ny + j] and inside[(nx - 1 - i) * ny + j])
either_x = sum(1 for i in range(nx) for j in range(ny) if inside[i * ny + j] or inside[(nx - 1 - i) * ny + j])
both_y = sum(1 for i in range(nx) for j in range(ny) if inside[i * ny + j] and inside[i * ny + (ny - 1 - j)])
either_y = sum(1 for i in range(nx) for j in range(ny) if inside[i * ny + j] or inside[i * ny + (ny - 1 - j)])
xs = [pts[k][0] for k in range(len(pts)) if inside[k]]
ys = [pts[k][1] for k in range(len(pts)) if inside[k]]
length, width = max(xs) - min(xs), max(ys) - min(ys)
overlap_x = both_x / max(either_x, 1)
overlap_y = both_y / max(either_y, 1)
print("final body in its middle plane: area %.1f mm^2 (the post %.1f), %.1f mm long (along the flow) and %.1f mm wide: %.2f times as long as wide" % (area, math.pi * (D / 2) ** 2, length, width, length / width))
print("overlap with its own mirror image front to back: %.3f of the union (Pironneau's body: 1); up to down: %.3f" % (overlap_x, overlap_y))

if overlap_x < OVERLAP_MIN:
    raise AssertionError("the final body is not the same in front and behind: overlap %.3f with its mirror image, less than %.2f" % (overlap_x, OVERLAP_MIN))
if length / width < ELONGATION_MIN:
    raise AssertionError("the final body is %.2f times as long as it is wide, less than %.1f" % (length / width, ELONGATION_MIN))
if abs(area / (math.pi * (D / 2) ** 2) - 1.0) > VOLUME_TOL:
    raise AssertionError("the final body holds %.1f mm^2 where the post held %.1f: more than %.0f %% apart" % (area, math.pi * (D / 2) ** 2, 100.0 * VOLUME_TOL))
if not design.real_drag < post_drag:
    raise AssertionError("the real drag of the final body %.4g N is not lower than the post's %.4g N" % (design.real_drag, post_drag))
print("ok")

design                                     # shown: the body in the flow
