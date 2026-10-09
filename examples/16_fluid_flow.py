# Fluid flow around a body: water past a round post in a channel, and the wake behind it.
#
# The flow goes around a body, inside a domain, as in flow_topology_optimization (this is that, solved once, for the body as it
# is): the body is the post, the domain a thin slab of water that holds the post's place (slip on both faces of the slab and on
# its sides: a two-dimensional flow, open at the sides).  fluid_analysis(body, domain, inlets, outlets, boundaries, fluid,
# element_size) solves the Navier-Stokes equations on tetrahedra that follow the fluid's surface (the domain with the body cut out).
# The boundary conditions are regions, an input for each kind: inlets (the mean speed, or a flow rate), outlets (their
# pressure), boundaries (walls, at rest unless they move, and slips, planes the fluid slides along: one or both, in one
# list); everything not named is a wall at rest -- here the post, so the wall force is the drag on it.
#
# The result on its own shows the flow: the fluid coloured by the speed, with streamlines from the inlet and particles
# moving along them drawn over it.  The result card switches to the pressure, the vorticity, ...; its step slider
# walks through the solver's iterations (the flow developing from the Stokes start); Flow hides the lines; the
# section card cuts the fluid open.  At Re = U D / nu = 40 the wake is steady: two standing eddies, about two
# diameters long, behind the post.
#
# Try: speed=10.0 (Re 100: the wake sheds vortices, so no steady flow exists -- the solver finds the unstable
# symmetric one), and then the flow in TIME: fluid_analysis(..., speed=10.0, ..., time=(4.0, 0.05)) solves 80 steps
# of 0.05 s from an impulsive start (the eddies grow, then the vortex street starts) and the result card gets a step
# slider with play / pause.  Also: air instead of water; a square post; two posts in line.
#
from fieldes import *

W, H, T = 80.0, 40.0, 4.0                  # the slab of fluid: 80 x 40 mm, 4 mm thick
D = 10.0                                   # the post's diameter
U = 4.0                                    # mm/s: Re = U D / nu = 40 in water (nu = 1 mm^2/s)

view.set_bounds((-4, -4, -4), (W + 4, H + 4, T + 4))
view.set_resolution(5)
view.set_quality(8)

post = cylinder_z(D / 2, T + 2, (24.0, 20.0, -1.0))
slab = box_exact((0, 0, 0), (W, H, T))                 # the domain: the water, and the place of the post in it

faces = union(box_exact((-1, -1, -1), (W + 1, H + 1, 0.01)), box_exact((-1, -1, T - 0.01), (W + 1, H + 1, T + 1)))
sides = union(box_exact((-1, -1, -1), (W + 1, 0.01, T + 1)), box_exact((-1, H - 0.01, -1), (W + 1, H + 1, T + 1)))
inlet_face = box_exact((-1, -1, -1), (0.01, H + 1, T + 1))
outlet_face = box_exact((W - 0.01, -1, -1), (W + 1, H + 1, T + 1))

flow = fluid_analysis(post, slab,
                      inlets=[inlet(inlet_face, speed=U)],
                      outlets=[outlet(outlet_face, pressure=0)],
                      boundaries=[slip(faces), slip(sides)],        # (the post is a wall at rest: everything not named is)
                      fluid=water, element_size=1.0)
print('drag on the post %.3g N (%.3g uN); speed behind it at 2 D: %.2f mm/s' % (
    flow.wall_force[0], flow.wall_force[0] * 1e6, flow.vx(24 + 2 * D, 20, T / 2)))

post                                       # the post, solid
flow                                       # the flow: the speed on the fluid, streamlines, moving particles
