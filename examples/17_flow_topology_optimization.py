# Flow topology optimization: the best body in a stream, found by the optimizer.
#
# A post of 10 mm stands in the middle of a slab of water that flows past it at 4 mm/s -- slowly, a creeping flow (stokes=True).  topology_optimization(flow, conditions=[...]) -- the call of every other
# analysis -- designs the body that a fluid_analysis(body, domain, ...) takes the flow around: the body is the design it starts from, the fluid domain the space it may fill.  What is asked is asked in the
# same list of conditions as anywhere: minimize(drag_force_1) makes the force along the flow as small as it can be, maximize(flow.lift_force) the force across it as large -- or a formula of results,
# maximize(lift_force_1 / drag_force_1) -- and keep_between(volume_fraction_1, 1.0, 1.0) keeps the body's own volume (keep_below(volume_fraction_1, 0.5) lets it use half).  keep= regions stay solid (a shaft,
# a mounting), avoid= regions stay fluid: here everything but a box round the post, so that the body is reshaped only where it is.  Its shape changes smoothly and its topology with it: it may shrink,
# grow, move, split or merge inside that box.
#
# THE RESULT: the circle becomes a lens, longer than it is wide, the same in front and behind, with pointed ends -- the body of the least drag for its volume in creeping flow, which Pironneau proved in 1973
# (J. Fluid Mech. 59, "On optimum profiles in Stokes flow").  The optimizer is not told so.  It finds by itself that the problem is symmetric about the planes along the flow and keeps the design symmetric
# about them (the output says so); that the lens is the same in front and behind it finds in the flow.  validation/pironneau_stokes_drag_minimal_body.py measures it.
#
# The design is one density for each element of the mesh (1 solid, 0 fluid) whose friction the flow feels; the real flow is solved once per iteration, and its exact adjoint once for each force the
# conditions read, to move the density on the optimizer of every other design.  A flow wants about 100 iterations (the default of every optimization is 20); the optimizer stops sooner when the design has
# stopped improving: here after 79.  About fourteen minutes at 1.0 mm elements on a 12-core machine (each iteration one flow solve and one adjoint solve, about 8 s; then the real flow around the
# final body is solved at 8 places of the mesh lattice, a tenth of the time); the next run reads the optimizer's result back from the result cache and takes about a minute and a half.
#
# The result on its own shows the body in the flow: the fluid coloured by the speed with streamlines, the body solid.  The result card steps through the iterations -- play to watch the body being reshaped
# and the wake behind it change; .drag is the force per iteration as the optimizer's model sees it (the body a friction in the flow), .real_drag the force of the REAL flow around the final body (the body a wall),
# .flow that flow (a FluidResult: fields, numbers, streamlines), .shape() the final body, .shape(iteration=k) the body after k.
#
# extrude='z' makes the body the same through the thickness (a 2D shape in a 2D flow).  Try: a square post; a smaller box, so that the body can only be reshaped where it is; no stokes=True, water with
# its inertia at 4 mm/s (Reynolds number 40, as in 16_fluid_flow.py): the best body is then not the same in front and behind.
from fieldes import *

W, H, T = 80.0, 40.0, 2.0                  # mm: the slab of water, two elements thick: the flow is two-dimensional (its faces are frictionless), so the thickness is only what the mesh needs
D = 10.0                                   # mm: the post
U = 4.0                                    # mm/s: the speed of the water at the inlet

view.set_bounds((-4, -4, -4), (W + 4, H + 4, T + 4), auto_scale=True)
view.set_resolution(25)
view.set_quality(8)

post = cylinder(D / 2, T + 2, position=(W / 2, H / 2, T / 2))
slab = box((W, H, T), position=(W / 2, H / 2, T / 2))
inlet_face = box((1.01, H + 2, T + 2), position=(-0.495, H / 2, T / 2))
inflow = inlet(inlet_face, speed=U)
outlet_face = box((1.01, H + 2, T + 2), position=(W + 0.495, H / 2, T / 2))
outflow = outlet(outlet_face, pressure=0)
faces = union(box((W + 2, H + 2, 1.01), position=(W / 2, H / 2, -0.495)), box((W + 2, H + 2, 1.01), position=(W / 2, H / 2, T + 0.495)))
slip_faces = slip(faces)                   # the frictionless faces of the slab: a two-dimensional flow
sides = union(box((W + 2, 1.01, T + 2), position=(W / 2, -0.495, T / 2)), box((W + 2, 1.01, T + 2), position=(W / 2, H + 0.495, T / 2)))
slip_sides = slip(sides)                   # the frictionless sides
flow = fluid_analysis(post, slab, inlets=[inflow], outlets=[outflow], boundaries=[slip_faces, slip_sides], fluid=water, element_size=1.0, stokes=True)
drag_force_1 = flow.drag_force
volume_fraction_1 = flow.volume_fraction
region = box((44, 24, T + 2), position=(W / 2, H / 2, T / 2))                              # where material may be: round the post
outside = difference(box((W + 2, H + 2, T + 2), position=(W / 2, H / 2, T / 2)), region)    # ... and everywhere else the fluid stays fluid
design = topology_optimization(flow, conditions=[minimize(drag_force_1), keep_between(volume_fraction_1, 1.0, 1.0)], iterations=100, avoid=outside, extrude='z')
print('%d iterations; the optimizer\'s own model of the drag %.3g -> %.3g N; the body keeps %.0f mm^3; the real flow around it: drag %.3g N' % (
    design.iterations, design.drag[0], design.drag[-1], design.volume, design.real_drag))

design                                     # shown: the body in the flow, iteration by iteration
