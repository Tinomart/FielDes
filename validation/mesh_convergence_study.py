# A MESH CONVERGENCE STUDY OF THE STATIC SOLVER, on a problem with a SINGULARITY and on the quantities that do and do not converge with it (H217).
#
# The method is the usual one for a grid convergence study (Roache; ASME V&V 20; NASA's grid convergence tutorial for CFD): the same problem on a series of meshes of one refinement ratio r, a quantity f read on each,
# the observed order of convergence p from the last three, the Richardson extrapolated value, and the grid convergence index GCI (the uncertainty band that the discretisation puts on the finest value, with the
# factor of safety 1.25 for three grids).  For a quantity that does NOT converge there is no such order: the value grows with every refinement, and the study says so.
#
#     p = ln((f3 - f2) / (f2 - f1)) / ln r              (f1 the finest grid, f3 the coarsest)
#     f_extrapolated = f1 + (f1 - f2) / (r^p - 1)
#     GCI = 1.25 |(f2 - f1) / f1| / (r^p - 1)
#
# The problem: an L-shaped plate 40 x 40 mm with arms of 20 mm, 2 mm thick, held across the end of its horizontal arm and pulled sideways at the end of its vertical arm, between two planes that hold it from moving
# through its thickness (plane strain).  Its re-entrant corner, at (20, 20), has a 270 degree opening: the stress there is singular, rising as r^(-0.4555) towards the corner (Williams; lambda = 0.5445 for a
# 270 degree corner), so the PEAK stress of any mesh is the stress of its smallest element and grows as the mesh is refined, while the strain energy (the compliance), the displacements and the stress at a FIXED
# distance from the corner converge.  A point at a fixed distance from a singularity is not singular: it is the number to quote.
#
# Open it in FielDes (from the links of Help > Validation and accuracy, or File > Open) and it runs: about 6 minutes with the linear elements; SIZES and ELEMENT are at the top ('tet10' for the ten-node element).
# Measured 2026-11-14 on the build of 15:58, linear tetrahedra: see docs/validation.md, "Mesh convergence study".
# THIS IS A CHECK OF THE PROGRAM, NOT A VALIDATION OF YOUR DESIGN: see the note at the end of docs/analysis.md.
#
import math

from fieldes import *

ELEMENT = 'tet'                         # 'tet' linear, 'tet10' ten-node
R = 1.4                                 # the refinement ratio between two meshes (element sizes 2.0, 1.43, 1.02, 0.73, 0.52)
SIZES = [2.0 / R ** k for k in range(5)]
T = 2.0                                 # mm, the thickness
A, W = 40.0, 20.0                       # mm, the sides of the L and the width of its arms
FORCE = 1000.0                          # N, the pull on the end of the vertical arm
DISTANCES = [1.5, 3.0]                  # mm, the fixed distances from the corner at which the stress is read

view.set_bounds((-5, -5, -5), (A + 5, A + 5, T + 5), auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

part = union(box((A, W, T), position=(A / 2, W / 2, T / 2)), box((W, A, T), position=(W / 2, A / 2, T / 2)))
held = box((1.0, W + 2, T + 2), position=(A + 0.5 - 0.01, W / 2, T / 2))               # the end of the horizontal arm, x = 40
pulled = box((W + 2, 1.0, T + 2), position=(W / 2, A + 0.5 - 0.01, T / 2))             # the end of the vertical arm, y = 40
plane_bottom = box((A + 2, A + 2, 1.0), position=(A / 2, A / 2, -0.5 + 0.01))          # z = 0: held from moving through the thickness
plane_top = box((A + 2, A + 2, 1.0), position=(A / 2, A / 2, T + 0.5 - 0.01))          # z = T
supports = [fixed(held), fixed(plane_bottom, displacement=(inf, inf, 0)), fixed(plane_top, displacement=(inf, inf, 0))]
loads = [force(pulled, (FORCE, 0, 0))]
corner = (W, W)


def at_distance(result, d):
    ''' the von Mises stress on the bisector of the re-entrant corner, d mm from it, in the middle of the thickness '''
    s = d / math.sqrt(2.0)
    return result.von_mises(corner[0] - s, corner[1] - s, T / 2)


def peak_near(result):
    ''' the highest von Mises stress on a ring of points 0.05 mm from the corner (what the smallest element there can show) '''
    best = 0.0
    for k in range(0, 270, 15):
        a = math.radians(90 + k)
        best = max(best, result.von_mises(corner[0] + 0.05 * math.cos(a), corner[1] + 0.05 * math.sin(a), T / 2))
    return best


rows = []
print('L-shaped plate, 270 degree re-entrant corner, elements %s, refinement ratio %.2f' % (ELEMENT, R))
print('%8s %9s %12s %14s %14s %14s %16s' % ('size mm', 'elements', 'compliance', 'max displ. mm', 'stress at %.1f' % DISTANCES[0], 'stress at %.1f' % DISTANCES[1], 'peak (0.05 mm)'))
for h in SIZES:
    r = static_analysis(part, supports=supports, loads=loads, material=steel, element_size=h, element=ELEMENT, cache=False)
    row = dict(h=h, elements=r.elements, compliance=float(r.compliance), displacement=float(r.max_displacement), s1=at_distance(r, DISTANCES[0]), s2=at_distance(r, DISTANCES[1]), peak=peak_near(r))
    rows.append(row)
    print('%8.3f %9d %12.6g %14.6g %14.4g %14.4g %16.4g' % (h, row['elements'], row['compliance'], row['displacement'], row['s1'], row['s2'], row['peak']), flush=True)


def study(name, key):
    f = [row[key] for row in rows]
    f1, f2, f3 = f[-1], f[-2], f[-3]
    d21, d32 = f2 - f1, f3 - f2
    if d21 == 0 or d32 == 0 or d32 / d21 <= 0:
        grows = f1 / f2 if f2 else float('nan')
        print('%-34s does NOT converge in the order sense: the last two differences %+.4g and %+.4g are not a falling series; the value grew by a factor %.3f over the last refinement' % (name, d32, d21, grows))
        return
    p = math.log(d32 / d21) / math.log(R)
    if p < 0.5:
        # (an order below one half is a series whose differences hardly shrink: the extrapolation and the GCI of the procedure mean nothing -- the study says so instead of printing them)
        print('%-34s NOT CONVERGED: observed order %.2f is under 0.5, the differences %+.4g and %+.4g hardly shrink; the finest value %.6g is not within any band of the value at zero element size' % (name, p, d32, d21, f1))
        return
    extrapolated = f1 + (f1 - f2) / (R ** p - 1.0)
    gci = 1.25 * abs((f2 - f1) / f1) / (R ** p - 1.0)
    print('%-34s observed order p = %.2f   extrapolated %.6g   finest %.6g   GCI of the finest %.2f %%' % (name, p, extrapolated, f1, 100 * gci))


print()
print('Observed order and uncertainty from the three finest meshes (Richardson, GCI):')
study('compliance (strain energy x 2)', 'compliance')
study('largest displacement', 'displacement')
study('stress %.1f mm from the corner' % DISTANCES[0], 's1')
study('stress %.1f mm from the corner' % DISTANCES[1], 's2')
study('peak stress at the corner (0.05 mm)', 'peak')
# the peak of a singularity grows as the element shrinks: sigma ~ h^(-(1 - lambda)), the exponent from the last two meshes
if len(rows) >= 2 and rows[-1]['peak'] > 0 and rows[-2]['peak'] > 0:
    slope = math.log(rows[-1]['peak'] / rows[-2]['peak']) / math.log(rows[-2]['h'] / rows[-1]['h'])
    print('the peak stress grows like h^-%.2f with the refinement (the theory for a 270 degree corner: h^-0.4555 while the elements are within the singular field)' % slope)
rows
