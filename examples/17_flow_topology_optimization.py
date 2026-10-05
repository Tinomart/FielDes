# Flow topology optimization: a body in a stream made best for the force the flow puts on it.
#
# The post of 16_fluid_flow.py sits in the same slab of water.  flow_topology_optimization(body, domain, conditions,
# fluid, objective, volume, region, ...) describes the body by a level set on the mesh (a smooth field whose zero level
# is the body's boundary) starting from the body as it is, solves the real flow (Navier-Stokes, with the body as a
# friction set by each element's share of it) once per iteration with its exact adjoint, and moves the boundary to
# make the objective best: 'drag' (the least force along the flow), 'lift' (the most force across it), or
# (w_drag, w_lift).  The restrictions: volume= what the body may use of its own volume (1.0 keeps it; (0.5, 1.5)
# bounds it), region= where material may be at all, keep= regions that stay solid (a shaft, a mounting), avoid=
# regions that stay fluid.  The body's shape changes smoothly and its topology with it: it may shrink, grow, move,
# split or merge inside the region.
#
# The result on its own shows the body in the flow: the fluid coloured by the speed with streamlines and moving
# particles, the body solid.  The result card steps through the iterations -- play to watch the body being reshaped and
# the wake behind it change; .drag and .lift are the force per iteration as the optimiser's model sees it (the body a
# friction in the flow), .flow the REAL flow around the final body (the body a wall; a FluidResult: fields, numbers,
# streamlines; .real_drag its force), .shape() the final body, .shape(iteration=k) the body after k, .level the level
# set itself (mm, positive inside the body).
#
# extrude='z' makes the body the same through the thickness (a 2D shape in a 2D flow).  Try: objective='lift' with
# volume=(0.8, 1.2); a square post; a smaller region so the body can only be reshaped where it is.
# About four minutes at 1.5 mm elements (one flow and one adjoint solve per iteration, then the real flow around the
# final body); the next run reads it back from the result cache at once.
#
from fieldes import *

W, H, T = 80.0, 40.0, 4.0                  # the slab of fluid: 80 x 40 mm, 4 mm thick
D = 10.0                                   # the post's diameter
U = 4.0                                    # mm/s: Re = U D / nu = 40 in water

view.set_bounds((-4, -4, -4), (W + 4, H + 4, T + 4))
view.set_resolution(5)
view.set_quality(8)

post = cylinder_z(D / 2, T + 2, (24.0, 20.0, -1.0))
slab = box_exact((0, 0, 0), (W, H, T))     # the fluid domain holds the body's place too
# hidden: slab
region = box_exact((12, 8, -1), (56, 32, T + 1))   # where material may be: around the post, well inside the slab
# hidden: region

faces = union(box_exact((-1, -1, -1), (W + 1, H + 1, 0.01)), box_exact((-1, -1, T - 0.01), (W + 1, H + 1, T + 1)))
sides = union(box_exact((-1, -1, -1), (W + 1, 0.01, T + 1)), box_exact((-1, H - 0.01, -1), (W + 1, H + 1, T + 1)))
inlet_face = box_exact((-1, -1, -1), (0.01, H + 1, T + 1))
outlet_face = box_exact((W - 0.01, -1, -1), (W + 1, H + 1, T + 1))
conditions = [slip(faces), slip(sides), inlet(inlet_face, speed=U), outlet(outlet_face, pressure=0)]

design = flow_topology_optimization(post, slab, conditions, fluid=water, objective='drag', volume=1.0, region=region,
                                    element_size=1.5, iterations=24, extrude='z')
print('drag %.3g -> %.3g N in the optimiser over %d iterations; the body keeps %.0f mm^3; '
      'the real flow around the final body: drag %.3g N (the round post: 8.2e-7 N, example 16)' % (
          design.drag[0], design.drag[-1], design.iterations, design.volume, design.real_drag))

design                                     # shown: the body in the flow, iteration by iteration
