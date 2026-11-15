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
# The mesh is refined four times by the same ratio (4/3); the observed order of convergence and the grid convergence index (Celik et al., J. Fluids Eng. 130, 2008) are printed for the drag, and the
# script stops with an error when the finest mesh is farther from the benchmark than the tolerances below.
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
# as a noise level and not as a converging series: -0.049, +0.065, +0.006, +0.019 and +0.026 at 2.0, 1.5, 1.0, 0.75 and 0.6 mm)
TOL_CD, TOL_PDIFF, TOL_CL, TOL_BALANCE = 0.04, 0.04, 0.03, 0.03

view.set_bounds((-4, -4, -4), (L + 4, H + 4, 12), auto_scale=True)
view.set_resolution(10)
view.set_quality(8)


def solve(h):
    T = 2.0 * h
    post = cylinder(D / 2, T + 2, position=(XC, YC, T / 2))
    channel = box((L, H, T), position=(L / 2, H / 2, T / 2))
    faces = union(box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, -0.495)), box((L + 2, H + 2, 1.01), position=(L / 2, H / 2, T + 0.495)))
    inlet_face = box((1.01, H + 2, T + 2), position=(-0.495, H / 2, T / 2))
    outlet_face = box((1.01, H + 2, T + 2), position=(L + 0.495, H / 2, T / 2))
    # the cylinder's own surface as a wall item: a thin shell round it (a region bigger than the cylinder would also take the ring of the frictionless faces round it for wall)
    shell = difference(cylinder(D / 2 + 0.3, T + 4, position=(XC, YC, T / 2)), cylinder(D / 2 - 0.3, T + 4, position=(XC, YC, T / 2)))
    flow = fluid_analysis(post, channel, inlets=[inlet(inlet_face, speed=U_MEAN, profile='developed')], outlets=[outlet(outlet_face, pressure=0)],
                          boundaries=[wall(shell), slip(faces)], fluid=water, element_size=h, cache=False)
    rho = water.density
    fx, fy, fz = flow.wall_forces[0]
    cd = 2.0 * fx / (rho * U_MEAN ** 2 * D * T)
    cl = 2.0 * fy / (rho * U_MEAN ** 2 * D * T)
    pdiff = (float(flow.pressure(XC - 5.0, YC, T / 2)) - float(flow.pressure(XC + 5.0, YC, T / 2))) / (rho * U_MEAN ** 2) * 0.2 ** 2     # in the units of the benchmark (rho 1, U_mean 0.2)
    balance = flow.pressure_drop * H * T / flow.wall_force[0]        # (the pressure drop over the channel against the force on all its walls: the momentum balance, 1 when the force is right)
    return flow, cd, cl, pdiff, balance


rows = []
print("DFG benchmark 2D-1 (Re = 20): the cylinder in the channel; the reference C_D %.6f, C_L %.6f, p_diff %.6f" % (CD_REF, CL_REF, PDIFF_REF))
print("  element mm   elements    C_D      error     C_L      p_diff    error    momentum balance")
for h in SIZES:
    flow, cd, cl, pdiff, balance = solve(h)
    rows.append((h, cd, cl, pdiff, balance))
    print("  %9.3f  %9d  %7.4f  %+7.2f %%  %+8.5f  %7.5f  %+7.2f %%   %.4f" % (h, flow.elements, cd, 100.0 * (cd / CD_REF - 1.0), cl, pdiff, 100.0 * (pdiff / PDIFF_REF - 1.0), balance))


def order_and_gci(f3, f2, f1, r):
    ''' The observed order p from three meshes (coarse f3, f2, fine f1) of the refinement ratio r, the value at zero element size and the GCI of the finest, Celik et al. 2008; None when the three do not change monotonically '''
    d1, d2 = f2 - f1, f3 - f2
    if d1 == 0.0 or d2 == 0.0 or d1 * d2 < 0.0:
        return None
    p = math.log(d2 / d1) / math.log(r)
    if p <= 0.0:
        return None
    return p, f1 + (f1 - f2) / (r ** p - 1.0), 1.25 * abs((f2 - f1) / f1) / (r ** p - 1.0)


r = SIZES[1] / SIZES[2]
got = order_and_gci(rows[1][1], rows[2][1], rows[3][1], r)
if got:
    print("drag, three finest meshes: observed order p = %.2f, value at zero element size %.4f, grid convergence index of the finest %.2f %%" % (got[0], got[1], 100.0 * got[2]))
else:
    print("drag, three finest meshes: no monotone series, no order is read from it")

h, cd, cl, pdiff, balance = rows[-1]
if abs(cd / CD_REF - 1.0) > TOL_CD:
    raise AssertionError("C_D %.4f is %.1f %% from the benchmark's %.4f: more than %.0f %%" % (cd, 100.0 * (cd / CD_REF - 1.0), CD_REF, 100.0 * TOL_CD))
if abs(pdiff / PDIFF_REF - 1.0) > TOL_PDIFF:
    raise AssertionError("p_diff %.5f is %.1f %% from the benchmark's %.5f: more than %.0f %%" % (pdiff, 100.0 * (pdiff / PDIFF_REF - 1.0), PDIFF_REF, 100.0 * TOL_PDIFF))
if abs(cl - CL_REF) > TOL_CL:
    raise AssertionError("C_L %.5f is %.4f from the benchmark's %.5f: more than %.2f" % (cl, abs(cl - CL_REF), CL_REF, TOL_CL))
if abs(balance - 1.0) > TOL_BALANCE:
    raise AssertionError("the momentum balance reads %.4f: more than %.0f %% from 1" % (balance, 100.0 * TOL_BALANCE))
print("ok")
