# Verification 23: the flow topology optimization (`topology_optimization` of a `fluid_analysis`, example 17) in creeping flow against what is known of the best body.
#
# There is no exact number to compare with for a body in a channel, but there is something that is known about the answer and that an optimizer that is wrong would break: in Stokes flow the equations are reversible,
# so the drag of a body is the same when the flow runs the other way, and in a domain that is a mirror image of itself, with the place the body may be in a mirror image of itself, the BEST body is the same in front
# and behind.  Pironneau (J. Fluid Mech. 59, 1973, "On optimum profiles in Stokes flow") showed that the body of the lowest drag for a given volume in Stokes flow is front-back symmetric, with constant vorticity on its
# surface and pointed ends (the front and the rear taper into cones of 120 degrees included angle for a body of revolution; read in the abstract of Montenegro-Johnson and Lauga, J. Fluid Mech. 2015,
# arXiv 1411.4783).  The best body of a flow that has inertia is not symmetric (for a body of revolution at Reynolds numbers of 100 and more Glowinski and Pironneau found a wedge of 90 degrees at the front and a cusp at the rear, as quoted
# by Chen et al., arXiv 2009.14339), which is why the test is in creeping flow, `stokes=True`.
#
# The set-up is that of example 17, which shows the result without the checks (a slab of water 80 x 40 x 2 mm, slip on the faces and the sides, a post of 10 mm that may be reshaped inside a box of 44 x 24 mm round it): the post and the box are in the MIDDLE of
# the slab, so that the problem is symmetric about the planes along the flow through the post (up-down, and across the slab): the optimizer detects them by itself, keeps the design exactly symmetric about them (its default:
# when a symmetry is detected every change of the design is made symmetric) and meshes about them.  The plane across the flow (front-back) is NOT one of them: the inlet and the outlet differ, so nothing makes the design
# symmetric there.  The optimizer minimizes the drag and keeps the post's volume.  The script then measures the final body in its middle plane and checks, with tolerances that say how loosely, that
#   - the body is the same in front and behind (overlap with its own mirror image over the length of the slab, at least 0.95 of the union), as Pironneau's body is: the optimizer is not told to make it so, it is a finding;
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

SIZE = 1.0                                 # mm: the element size (1.5 gave a body only nine elements long, with a faceted outline)
W, H, T = 80.0, 40.0, 2.0 * SIZE           # mm: the slab of water, two elements thick: the flow is two-dimensional (the faces are frictionless), so the thickness is only what the mesh needs
D = 10.0                                   # mm: the post
U = 4.0                                    # mm/s
OVERLAP_MIN, ELONGATION_MIN, VOLUME_TOL = 0.95, 1.4, 0.03

view.set_bounds((-4, -4, -4), (W + 4, H + 4, T + 4), auto_scale=True)
view.set_resolution(25)                    # ten samples across the longest side of a model for every unit: 250 across the region of 80 mm, 3 samples per mm for the body (16 mm long); the fluid is drawn from the triangles of its own mesh and is not meshed from a field, so the resolution costs it nothing
view.set_quality(8)

post = cylinder(D / 2, T + 2, position=(W / 2, H / 2, -1.0 + (T + 2) / 2))
slab = box((W, H, T), position=(W / 2, H / 2, T / 2))
# every condition stands directly above the analysis that takes it, so that it is a row of its own under the flow's "boundary conditions" and has its own eye
inlet_face = box((1.01, H + 2, T + 2), position=(-0.495, H / 2, T / 2))
inflow = inlet(inlet_face, speed=U)                                       # the inlet: 4 mm/s
outlet_face = box((1.01, H + 2, T + 2), position=(W + 0.495, H / 2, T / 2))
outflow = outlet(outlet_face, pressure=0)                                 # the outlet: pressure 0
faces = union(box((W + 2, H + 2, 1.01), position=(W / 2, H / 2, -0.495)), box((W + 2, H + 2, 1.01), position=(W / 2, H / 2, T + 0.495)))
slip_faces = slip(faces)                                                  # the frictionless faces of the slab: a two-dimensional flow
sides = union(box((W + 2, 1.01, T + 2), position=(W / 2, -0.495, T / 2)), box((W + 2, 1.01, T + 2), position=(W / 2, H + 0.495, T / 2)))
slip_sides = slip(sides)                                                  # the frictionless sides
flow = fluid_analysis(post, slab, inlets=[inflow], outlets=[outflow], boundaries=[slip_faces, slip_sides], fluid=water, element_size=SIZE, stokes=True, cache=False)
drag_force_1 = flow.drag_force
volume_fraction_1 = flow.volume_fraction
post_drag = float(flow.wall_force[0])
print("the post in creeping flow: drag %.4g N" % post_drag)
region = box((44, 24, T + 2), position=(W / 2, H / 2, T / 2))                                                   # where material may be: round the post, in the middle of the slab
outside = difference(box((W + 2, H + 2, T + 2), position=(W / 2, H / 2, T / 2)), region)                          # ... and everywhere else the fluid stays fluid
design = topology_optimization(flow, conditions=[minimize(drag_force_1), keep_between(volume_fraction_1, 1.0, 1.0)], iterations=100, avoid=outside, extrude='z', cache=False)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the optimizer's result (a row of its own, its eye shows or hides it; the iterations play on the result card).  The conditions it was solved with
# (`inflow`, `outflow`, `slip_faces`, `slip_sides`) are rows of its branch of the model tree with their eyes off, because their arrows and discs cover the body: switch one on there to see where it acts
design                                     # the final body in the flow: the fluid coloured by the speed, with streamlines

print("optimizer: %d iterations; the optimizer's own model of the drag %.4g -> %.4g N (%+.0f %%); the real flow: the post %.4g N, the final body %.4g N (%+.1f %%)" % (
    design.iterations, design.drag[0], design.drag[-1], 100.0 * (design.drag[-1] / design.drag[0] - 1.0), post_drag, design.real_drag, 100.0 * (design.real_drag / post_drag - 1.0)))
print("is it still improving: the optimizer's own model of the drag over its last ten iterations %.4g -> %.4g N (%+.2f %%)" % (
    design.drag[-11], design.drag[-1], 100.0 * (design.drag[-1] / design.drag[-11] - 1.0)))

# the final body in its middle plane, on a grid of half a millimetre, and its mirror images.  (The final body, design.shape(), is used inline and not kept in a variable: a variable that holds a shape is
# a model of its own in the tree, made of `design`, and would take the optimizer's result, the flow and the conditions into its closed row)
step = 0.5
nx, ny = int(W / step), int(H / step)
pts = [((i + 0.5) * step, (j + 0.5) * step, T / 2) for i in range(nx) for j in range(ny)]
vals = [v for s in range(0, len(pts), 20000) for v in evaluate(design.shape(), pts[s:s + 20000])]
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
real_drag = float(design.real_drag)       # (a number read from the result: an `if` that read `design` itself would take its row into its own)
if not real_drag < post_drag:
    raise AssertionError("the real drag of the final body %.4g N is not lower than the post's %.4g N" % (real_drag, post_drag))
print("ok")
