# Verification 22: flow AROUND A BODY -- the cylinder in a channel of the DFG benchmark 2D-1 (Schaefer and Turek, 1996), laminar, steady, Reynolds number 20.
#
# This is the standard problem for a flow solver that has a body in the stream: a circular cylinder (diameter D = 0.1) not quite in the middle of a channel 2.2 long and 0.41 wide, a parabolic inflow
# with the maximum speed 0.3 (the mean speed 0.2) so that Re = U_mean D / nu = 20, nu = 0.001, density 1, no-slip on the channel walls and the cylinder, "do-nothing" at the outlet.  The set-up is taken from
# the page of the CFD benchmarking project of the Featflow group of TU Dortmund (https://wwwold.mathematik.tu-dortmund.de/~featflow/en/benchmarks/cfdbenchmarking/flow/dfg_benchmark1_re20.html), which also
# gives the reference values, computed with spectral methods (Nabh, Preprint 42/98, University of Heidelberg, 1998):
#     drag coefficient     C_D    = 5.57953523384    (the force on the cylinder along the flow, 2 F / (rho U_mean^2 D L))
#     lift coefficient     C_L    = 0.010618948146   (across the flow)
#     pressure difference  p_diff = 0.11752016697    (the pressure at the front of the cylinder, (0.15, 0.2), minus the one behind it, (0.25, 0.2))
# Every length of the benchmark is scaled by 0.1 here (the cylinder is 10 mm, the channel 220 x 41 mm; the cylinder's centre is 20 mm from the inlet and 20 mm from the lower wall) and the fluid is water
# (nu = 1 mm^2/s), so the mean speed that gives Re = 20 is 2 mm/s; the coefficients and the pressure difference are dimensionless in the benchmark's units and are compared as such.  The flow is
# two-dimensional: the domain is a slab two elements thick with a frictionless face on each side.  The force on the cylinder is read from the wall item that is the cylinder's own surface.
#
# The mesh is refined four times by the same ratio (4/3): the four meshes are the four blocks below, each its own flow analysis (`flow_1` the coarsest ... `flow_4` the finest) with the conditions it was solved with.  The observed
# order of convergence and the grid convergence index (Celik et al., J. Fluids Eng. 130, 2008) are printed for the drag, and the script stops with an error when the finest mesh is farther from the benchmark than the tolerances below.
# WHAT YOU SEE when you open it: the water round the cylinder of the finest mesh (`flow_4`), coloured by the speed with its streamlines, with the inlet, the outlet, the cylinder's wall and the frictionless faces drawn on it.
# The other three meshes are rows of the model tree: their eyes show them.
#
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
from fieldes import *
import math

L, H, D = 220.0, 41.0, 10.0           # mm: the channel (2.2 x 0.41 in the benchmark) and the cylinder (0.1), all lengths times 0.1
XC, YC = 20.0, 20.0                   # mm: the centre of the cylinder
U_MEAN = 2.0                          # mm/s: Re = U_mean D / nu = 2 x 10 / 1 = 20 in water
SIZES = [16.0 / 9.0, 4.0 / 3.0, 1.0, 0.75]      # mm: the element sizes, each 3/4 of the one before
CD_REF, CL_REF, PDIFF_REF = 5.57953523384, 0.010618948146, 0.11752016697
# (C_D and p_diff: the part of the reference; C_L: the absolute difference -- the lift of this symmetric-looking flow is a small number that the mesh scatters by a few hundredths from one size to the next, so it is read
# as a noise level and not as a converging series: -0.046, +0.012, +0.005 and +0.019 at 1.78, 1.33, 1.0 and 0.75 mm)
TOL_CD, TOL_PDIFF, TOL_CL, TOL_BALANCE = 0.04, 0.04, 0.03, 0.03

view.set_bounds((-4, -4, -4), (L + 4, H + 4, 12), auto_scale=True)
view.set_resolution(15)                 # ten samples across the longest side of a model for every unit: 150 across the slab of 220 mm, cells of 1.5 mm, 7 across the cylinder; the flow itself is drawn with the elements of the analysis
view.set_quality(8)

# MESH 1, the coarsest: the slab is two elements thick, T = 2 h.  Every condition stands directly above the analysis that takes it.
T_1 = 2.0 * SIZES[0]
post_1 = cylinder(D / 2, T_1 + 2, position=(XC, YC, T_1 / 2))
channel_1 = box((L, H, T_1), position=(L / 2, H / 2, T_1 / 2))
inflow_1 = inlet(box((1.01, H + 2, T_1 + 2), position=(-0.495, H / 2, T_1 / 2)), speed=U_MEAN, profile='developed')
outflow_1 = outlet(box((1.01, H + 2, T_1 + 2), position=(L + 0.495, H / 2, T_1 / 2)), pressure=0)
# the cylinder's own surface as a wall item: a thin shell round it (a region bigger than the cylinder would also take the ring of the frictionless faces round it for wall)
cylinder_wall_1 = wall(difference(cylinder(D / 2 + 0.3, T_1 + 4, position=(XC, YC, T_1 / 2)), cylinder(D / 2 - 0.3, T_1 + 4, position=(XC, YC, T_1 / 2))))
sides_1 = slip(union(box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, -0.495)), box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, T_1 + 0.495))))
flow_1 = fluid_analysis(post_1, channel_1, inlets=[inflow_1], outlets=[outflow_1], boundaries=[cylinder_wall_1, sides_1], fluid=water, element_size=SIZES[0], cache=False)

# MESH 2
T_2 = 2.0 * SIZES[1]
post_2 = cylinder(D / 2, T_2 + 2, position=(XC, YC, T_2 / 2))
channel_2 = box((L, H, T_2), position=(L / 2, H / 2, T_2 / 2))
inflow_2 = inlet(box((1.01, H + 2, T_2 + 2), position=(-0.495, H / 2, T_2 / 2)), speed=U_MEAN, profile='developed')
outflow_2 = outlet(box((1.01, H + 2, T_2 + 2), position=(L + 0.495, H / 2, T_2 / 2)), pressure=0)
cylinder_wall_2 = wall(difference(cylinder(D / 2 + 0.3, T_2 + 4, position=(XC, YC, T_2 / 2)), cylinder(D / 2 - 0.3, T_2 + 4, position=(XC, YC, T_2 / 2))))
sides_2 = slip(union(box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, -0.495)), box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, T_2 + 0.495))))
flow_2 = fluid_analysis(post_2, channel_2, inlets=[inflow_2], outlets=[outflow_2], boundaries=[cylinder_wall_2, sides_2], fluid=water, element_size=SIZES[1], cache=False)

# MESH 3
T_3 = 2.0 * SIZES[2]
post_3 = cylinder(D / 2, T_3 + 2, position=(XC, YC, T_3 / 2))
channel_3 = box((L, H, T_3), position=(L / 2, H / 2, T_3 / 2))
inflow_3 = inlet(box((1.01, H + 2, T_3 + 2), position=(-0.495, H / 2, T_3 / 2)), speed=U_MEAN, profile='developed')
outflow_3 = outlet(box((1.01, H + 2, T_3 + 2), position=(L + 0.495, H / 2, T_3 / 2)), pressure=0)
cylinder_wall_3 = wall(difference(cylinder(D / 2 + 0.3, T_3 + 4, position=(XC, YC, T_3 / 2)), cylinder(D / 2 - 0.3, T_3 + 4, position=(XC, YC, T_3 / 2))))
sides_3 = slip(union(box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, -0.495)), box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, T_3 + 0.495))))
flow_3 = fluid_analysis(post_3, channel_3, inlets=[inflow_3], outlets=[outflow_3], boundaries=[cylinder_wall_3, sides_3], fluid=water, element_size=SIZES[2], cache=False)

# MESH 4, the finest
T_4 = 2.0 * SIZES[3]
post_4 = cylinder(D / 2, T_4 + 2, position=(XC, YC, T_4 / 2))
channel_4 = box((L, H, T_4), position=(L / 2, H / 2, T_4 / 2))
inflow_4 = inlet(box((1.01, H + 2, T_4 + 2), position=(-0.495, H / 2, T_4 / 2)), speed=U_MEAN, profile='developed')
outflow_4 = outlet(box((1.01, H + 2, T_4 + 2), position=(L + 0.495, H / 2, T_4 / 2)), pressure=0)
cylinder_wall_4 = wall(difference(cylinder(D / 2 + 0.3, T_4 + 4, position=(XC, YC, T_4 / 2)), cylinder(D / 2 - 0.3, T_4 + 4, position=(XC, YC, T_4 / 2))))
sides_4 = slip(union(box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, -0.495)), box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, T_4 + 0.495))))
flow_4 = fluid_analysis(post_4, channel_4, inlets=[inflow_4], outlets=[outflow_4], boundaries=[cylinder_wall_4, sides_4], fluid=water, element_size=SIZES[3], cache=False)

# SHOWN IN THE VIEWPORT, directly after the models and before any check: the flow of the finest mesh (the eye of its row in the model tree shows or hides it; `flow_1` ... `flow_3` are the coarser meshes).  The conditions it was
# solved with (`inflow_4`: the developed profile at a mean speed of 2 mm/s, `outflow_4`: pressure 0, `cylinder_wall_4`: the cylinder's own surface, no-slip, the force on it is the drag, `sides_4`: the frictionless faces of the slab)
# are rows of the model tree with their eyes off, because their arrows and discs cover the water and the cylinder: switch one on there
flow_4                                  # the water round the cylinder, coloured by the speed, with its streamlines


def numbers(flow, T):
    ''' the drag coefficient, the lift coefficient, the pressure difference (in the units of the benchmark) and the momentum balance of one flow analysis of the slab thickness T (mm); numbers only, no model '''
    rho = water.density
    fx, fy, fz = flow.wall_forces[0]
    cd = 2.0 * fx / (rho * U_MEAN ** 2 * D * T)
    cl = 2.0 * fy / (rho * U_MEAN ** 2 * D * T)
    pdiff = (float(flow.pressure(XC - 5.0, YC, T / 2)) - float(flow.pressure(XC + 5.0, YC, T / 2))) / (rho * U_MEAN ** 2) * 0.2 ** 2     # in the units of the benchmark (rho 1, U_mean 0.2)
    balance = flow.pressure_drop * H * T / flow.wall_force[0]        # (the pressure drop over the channel against the force on all its walls: the momentum balance, 1 when the force is right)
    return cd, cl, pdiff, balance


def order_and_gci(f3, f2, f1, r):
    ''' The observed order p from three meshes (coarse f3, f2, fine f1) of the refinement ratio r, the value at zero element size and the GCI of the finest, Celik et al. 2008; None when the three do not change monotonically '''
    d1, d2 = f2 - f1, f3 - f2
    if d1 == 0.0 or d2 == 0.0 or d1 * d2 < 0.0:
        return None
    p = math.log(d2 / d1) / math.log(r)
    if p <= 0.0:
        return None
    return p, f1 + (f1 - f2) / (r ** p - 1.0), 1.25 * abs((f2 - f1) / f1) / (r ** p - 1.0)


flows = (flow_1, flow_2, flow_3, flow_4)
thicknesses = (T_1, T_2, T_3, T_4)
rows = [numbers(f, t) for f, t in zip(flows, thicknesses)]
print("DFG benchmark 2D-1 (Re = 20): the cylinder in the channel; the reference C_D %.6f, C_L %.6f, p_diff %.6f" % (CD_REF, CL_REF, PDIFF_REF))
print("  element mm   elements    C_D      error     C_L      p_diff    error    momentum balance")
print('\n'.join("  %9.3f  %9d  %7.4f  %+7.2f %%  %+8.5f  %7.5f  %+7.2f %%   %.4f" % (h, f.elements, cd, 100.0 * (cd / CD_REF - 1.0), cl, pdiff, 100.0 * (pdiff / PDIFF_REF - 1.0), balance) for h, f, (cd, cl, pdiff, balance) in zip(SIZES, flows, rows)))

got = order_and_gci(rows[1][0], rows[2][0], rows[3][0], SIZES[1] / SIZES[2])
if got:
    print("drag, three finest meshes: observed order p = %.2f, value at zero element size %.4f, grid convergence index of the finest %.2f %%" % (got[0], got[1], 100.0 * got[2]))
else:
    print("drag, three finest meshes: no monotone series, no order is read from it")

cd, cl, pdiff, balance = rows[-1]
assert abs(cd / CD_REF - 1.0) <= TOL_CD, "C_D %.4f is %.1f %% from the benchmark's %.4f: more than %.0f %%" % (cd, 100.0 * (cd / CD_REF - 1.0), CD_REF, 100.0 * TOL_CD)
assert abs(pdiff / PDIFF_REF - 1.0) <= TOL_PDIFF, "p_diff %.5f is %.1f %% from the benchmark's %.5f: more than %.0f %%" % (pdiff, 100.0 * (pdiff / PDIFF_REF - 1.0), PDIFF_REF, 100.0 * TOL_PDIFF)
assert abs(cl - CL_REF) <= TOL_CL, "C_L %.5f is %.4f from the benchmark's %.5f: more than %.2f" % (cl, abs(cl - CL_REF), CL_REF, TOL_CL)
assert abs(balance - 1.0) <= TOL_BALANCE, "the momentum balance reads %.4f: more than %.0f %% from 1" % (balance, 100.0 * TOL_BALANCE)
print("ok")
