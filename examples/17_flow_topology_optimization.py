# Flow topology optimization: a body in a stream made best for the force the flow puts on it.
#
# The post of 16_fluid_flow.py sits in the same slab of water.  topology_optimization(flow, conditions=[...]) -- the call of every other analysis --
# designs the body that a fluid_analysis(body, domain, ...) takes the flow around: the body is the design it starts from, the fluid domain the space it may
# fill.  What is asked is asked in the same list of conditions as anywhere: minimize(drag_force_1) makes the force along the flow as small as it can be,
# maximize(flow.lift_force) the force across it as large -- or a formula of results, maximize(lift_force_1 / drag_force_1) -- and keep_between(volume_fraction_1, 1.0, 1.0) keeps
# the body's own volume (keep_below(volume_fraction_1, 0.5) lets it use half).  keep= regions stay solid (a shaft, a mounting), avoid= regions stay fluid: here everything but a box
# round the post, so that the body is reshaped only where it is.  Its shape changes smoothly and its topology with it: it may shrink, grow, move, split or
# merge inside that box.
#
# The design is one density for each element of the mesh (1 solid, 0 fluid) whose friction the flow feels; the real flow (Navier-Stokes) is solved once per
# iteration, and its exact adjoint once for each force the constraints read, to move the density on the optimiser of every other design.  The friction of an element follows its density
# linearly and the design is sharpened a little at every iteration: a flow wants about 80 iterations (the default for a flow).
#
# The result on its own shows the body in the flow: the fluid coloured by the speed with streamlines and moving particles, the body solid.  The result card
# steps through the iterations -- play to watch the body being reshaped and the wake behind it change; .drag and .lift are the force per iteration as the
# optimiser's model sees it (the body a friction in the flow), .flow the REAL flow around the final body (the body a wall; a FluidResult: fields, numbers,
# streamlines; .real_drag its force: the mean over several places of the mesh lattice), .shape() the final body, .shape(iteration=k) the body after k, .values and .table() what was asked, as the final body has it.
#
# extrude='z' makes the body the same through the thickness (a 2D shape in a 2D flow).  Try: maximize the lift force (flow.lift_force) with keep_below(volume_fraction_1, 1.2); a square
# post; a smaller box so the body can only be reshaped where it is.
# About four minutes at 1.5 mm elements on a 12-core machine: the optimiser stops when the design has stopped improving (here after 65 of the 80 iterations, each one flow solve and one
# adjoint solve), then the real flow around the final body is solved at 8 places of the mesh lattice, a fifth of the time.  With 2 mm elements it takes less than half as long, but the
# lens is then thinner than the mesh can hold (fluid_analysis says the mesh is coarse, and the drag of the lens is known to about 15 % only: docs/analysis.md), and part of the volume is
# left in small pieces ahead of it -- the result says so; the next run reads it back from the result cache at once.
#
from fieldes import *

W, H, T = 80.0, 40.0, 4.0                  # the slab of fluid: 80 x 40 mm, 4 mm thick
D = 10.0                                   # the post's diameter
U = 4.0                                    # mm/s: Re = U D / nu = 40 in water

view.set_bounds((-4, -4, -4), (W + 4, H + 4, T + 4), auto_scale=True)
view.set_resolution(25)
view.set_quality(8)

post = cylinder(D / 2, T + 2, position=(24.0, 20.0, -1.0 + (T + 2) / 2))
slab = box((W, H, T), position=(W / 2, H / 2, T / 2))     # the fluid domain holds the body's place too
# hidden: slab
region = box((44, 24, (T + 1) - (-1)), position=(34, 20, ((-1) + (T + 1)) / 2))   # where material may be: around the post, well inside the slab
# hidden: region
outside = difference(box(((W + 1) - (-1), (H + 1) - (-1), (T + 1) - (-1)), position=(((-1) + (W + 1)) / 2, ((-1) + (H + 1)) / 2, ((-1) + (T + 1)) / 2)), region)   # ... and everywhere else the fluid stays fluid
# hidden: outside

faces = union(box(((W + 1) - (-1), (H + 1) - (-1), 1.01), position=(((-1) + (W + 1)) / 2, ((-1) + (H + 1)) / 2, -0.495)), box(((W + 1) - (-1), (H + 1) - (-1), (T + 1) - (T - 0.01)), position=(((-1) + (W + 1)) / 2, ((-1) + (H + 1)) / 2, ((T - 0.01) + (T + 1)) / 2)))
sides = union(box(((W + 1) - (-1), 1.01, (T + 1) - (-1)), position=(((-1) + (W + 1)) / 2, -0.495, ((-1) + (T + 1)) / 2)), box(((W + 1) - (-1), (H + 1) - (H - 0.01), (T + 1) - (-1)), position=(((-1) + (W + 1)) / 2, ((H - 0.01) + (H + 1)) / 2, ((-1) + (T + 1)) / 2)))
inlet_face = box((1.01, (H + 1) - (-1), (T + 1) - (-1)), position=(-0.495, ((-1) + (H + 1)) / 2, ((-1) + (T + 1)) / 2))
outlet_face = box(((W + 1) - (W - 0.01), (H + 1) - (-1), (T + 1) - (-1)), position=(((W - 0.01) + (W + 1)) / 2, ((-1) + (H + 1)) / 2, ((-1) + (T + 1)) / 2))

flow = fluid_analysis(post, slab,
                      inlets=[inlet(inlet_face, speed=U)], outlets=[outlet(outlet_face, pressure=0)],
                      boundaries=[slip(faces), slip(sides)],
                      fluid=water, element_size=1.5)
drag_force_1 = flow.drag_force
volume_fraction_1 = flow.volume_fraction
design = topology_optimization(flow, conditions=[minimize(drag_force_1), keep_between(volume_fraction_1, 1.0, 1.0)], avoid=outside, extrude='z')
print('drag %.3g -> %.3g N in the optimiser (the same model for the post and for the final body) over %d iterations; the body keeps %.0f mm^3; '
      'the real flow around the final body: drag %.3g N, the mean of %d places of the mesh lattice (they differ by %.0f %%)' % (
          design.drag[0], design.drag[-1], design.iterations, design.volume, design.real_drag, len(design.real_drags),
          100.0 * (max(design.real_drags) - min(design.real_drags)) / design.real_drag))

design                                     # shown: the body in the flow, iteration by iteration
