# Fields everywhere: for every numeric slot of the library, a CONSTANT FIELD in place of the number must give the very same
# result (and a field that varies must at least build and evaluate).  Prints one line per slot: ok / the error.
import math
import random
import sys
from fieldes import *
from fieldes.stdlib import fields as F

random.seed(7)
PTS = [(random.uniform(-9, 9), random.uniform(-9, 9), random.uniform(-9, 9)) for _ in range(40)]
x0 = x_field()


def const(v):
    ''' A field that is v everywhere (a tree, not a number) '''
    return x0 * 0.0 + v


def vary(v):
    ''' A field that is about v and varies in space (small, so what it is put in stays valid) '''
    return v * (1.0 + 0.02 * (x_field() * 0.1).sin())


def vals(shape):
    s = shape if isinstance(shape, Shape) else Shape.wrap(shape)
    return evaluate(s, PTS)


bad = []
rows = []


def check(label, build, base):
    ''' build(v) makes the thing from a number or a field v; base is the number '''
    try:
        a = vals(build(base))
    except Exception as e:
        rows.append((label, 'NUMBER FAILED: %s' % str(e)[:90]))
        bad.append(label)
        return
    try:
        b = vals(build(const(base)))
    except Exception as e:
        rows.append((label, 'FIELD REFUSED: %s: %s' % (type(e).__name__, str(e)[:90])))
        bad.append(label)
        return
    worst = max(abs(u - v) for u, v in zip(a, b))
    if not worst < 1e-3 * (1 + max(abs(u) for u in a)):
        rows.append((label, 'DIFFERENT (worst %.3g)' % worst))
        bad.append(label)
        return
    try:
        vals(build(vary(base)))
    except Exception as e:
        rows.append((label, 'VARYING FIELD FAILED: %s' % str(e)[:90]))
        bad.append(label)
        return
    rows.append((label, 'ok'))


sph = sphere(5)
bx = box_exact((-4, -4, -4), (4, 4, 4))

# --- primitives
check('sphere(radius)', lambda v: sphere(v), 5.0)
check('box_exact_centered(size)', lambda v: box_exact_centered((v, v, v)), 6.0)
check('box_exact(a, b)', lambda v: box_exact((-v, -v, -v), (v, v, v)), 4.0)
check('box(a, b)', lambda v: box((-v, -v, -v), (v, v, v)), 4.0)
check('box_centered(size)', lambda v: box_centered((v, v, v)), 6.0)
check('rounded_box(a, b, r)', lambda v: rounded_box((-4, -4, -4), (4, 4, 4), v), 0.3)
check('cylinder_z(r, h)', lambda v: cylinder_z(v, v, (0, 0, -3)), 4.0)
check('cone_z(r, h)', lambda v: cone_z(v, v, (0, 0, -3)), 4.0)
check('cone_ang_z(angle, h)', lambda v: cone_ang_z(v, 6, (0, 0, -3)), 0.5)
check('torus_z(ro, ri)', lambda v: torus_z(v, 1.5), 5.0)
check('torus_z(ri)', lambda v: torus_z(5.0, v), 1.5)
check('circle(r)', lambda v: circle(v), 5.0)
check('ring(ro, ri)', lambda v: ring(v, 2.0), 5.0)
check('polygon(r)', lambda v: polygon(v, 6), 5.0)
check('rectangle(a, b)', lambda v: rectangle((-v, -v), (v, v)), 4.0)
check('rounded_rectangle(r)', lambda v: rounded_rectangle((-4, -4), (4, 4), v), 0.3)
check('half_space(point)', lambda v: half_space((0, 0, 1), (0, 0, v)), 1.0)
check('gyroid(period, thickness)', lambda v: gyroid((v, v, v), 0.5), 8.0)
check('gyroid(thickness)', lambda v: gyroid((8.0, 8.0, 8.0), v), 0.5)
check('pyramid_z(zmin, height)', lambda v: pyramid_z((-3, -3), (3, 3), -3, v), 6.0)
check('extrude_z(zmin, zmax)', lambda v: extrude_z(circle(3), -v, v), 3.0)

# --- csg
check('blend(a, b, m)', lambda v: blend(sph, bx, v), 0.5)
check('blend_difference(m)', lambda v: blend_difference(bx, sph, v), 0.5)
check('clearance(offset)', lambda v: clearance(bx, sph, v), 0.5)
check('morph(a, b, m)', lambda v: morph(sph, bx, v), 0.5)
check('offset(a, o)', lambda v: offset(bx, v), 0.5)
check('shell(a, offset)', lambda v: shell(bx, v), 0.5)
check('union(radius)', lambda v: union(sph, move(bx, (3, 0, 0)), radius=v), 0.5)
check('loft(zmin, zmax)', lambda v: loft(circle(3), circle(2), -v, v), 3.0)

# --- transforms
check('move(offset)', lambda v: move(bx, (v, 0.5, 0)), 1.5)
check('rotate_z(angle)', lambda v: rotate_z(bx, v), 0.4)
check('rotate_x(angle)', lambda v: rotate_x(bx, v), 0.4)
check('scale_x(factor)', lambda v: scale_x(bx, v), 1.2)
check('scale_xyz(factors)', lambda v: scale_xyz(bx, (v, v, v)), 1.2)
check('reflect_x(plane)', lambda v: reflect_x(move(bx, (2, 0, 0)), v), 1.0)
check('attract(radius)', lambda v: attract(sph, (0, 0, 0), v), 8.0)
check('attract(exaggerate)', lambda v: attract(sph, (0, 0, 0), 8.0, v), 1.0)
check('repel(radius)', lambda v: repel(sph, (0, 0, 0), v), 8.0)
check('twirl_z(amount)', lambda v: twirl_z(bx, v, 8.0), 0.5)
check('twirl_z(radius)', lambda v: twirl_z(bx, 0.5, v), 8.0)
check('taper_xy_z(scale)', lambda v: taper_xy_z(bx, (0, 0, -4), 8.0, v), 0.5)
check('shear_x_y(offset)', lambda v: shear_x_y(bx, (0.0, -4.0), 8.0, v), 2.0)
check('revolve_y(x0)', lambda v: revolve_y(circle(1.5, (4, 0)), v), 0.0)

# --- fields
f = x_field() * 0.5 + z_field() * 0.2
check('clamp(lo)', lambda v: clamp(f, v, 3.0), -2.0)
check('clamp(hi)', lambda v: clamp(f, -3.0, v), 3.0)
check('ramp(input_range)', lambda v: ramp(f, (-v, v), (0.5, 2.0)), 4.0)
check('ramp(output_range)', lambda v: ramp(f, (-4.0, 4.0), (v, 2.0)), 0.5)
check('normalize(lo, hi)', lambda v: normalize(f, -v, v), 4.0)
check('lerp(t)', lambda v: lerp(f, 3.0, v), 0.3)
check('lerp(a)', lambda v: lerp(v, f, 0.3), 1.0)
check('smoothstep(edge0)', lambda v: smoothstep(f, v, 3.0), -3.0)
check('step_field(edge)', lambda v: step_field(f, v), 0.5)
check('attractor(radius)', lambda v: attractor([(0, 0, 0)], v), 6.0)
check('attractor(strength)', lambda v: attractor([(0, 0, 0)], 6.0, strength=v), 2.0)
check('wave(period)', lambda v: wave('x', v), 10.0)
check('wave(amplitude)', lambda v: wave('x', 10.0, v), 2.0)
check('wave(phase)', lambda v: wave('x', 10.0, 1.0, v), 0.5)
check('thicken(thickness)', lambda v: thicken(bx, v), 1.0)
check('shell_inside(thickness)', lambda v: shell_inside(bx, v), 1.0)
check('shell_outside(thickness)', lambda v: shell_outside(bx, v), 1.0)
check('shell_centered(thickness)', lambda v: shell_centered(bx, v), 1.0)
check('offset_by(distance)', lambda v: offset_by(bx, v), 0.5)
check('smooth_union(radius)', lambda v: smooth_union(sph, move(bx, (3, 0, 0)), v), 1.0)
check('smooth_intersection(radius)', lambda v: smooth_intersection(sph, bx, v), 1.0)
check('smooth_difference(radius)', lambda v: smooth_difference(bx, sph, v), 1.0)
check('chamfer_union(size)', lambda v: chamfer_union(sph, move(bx, (3, 0, 0)), v), 1.0)
check('union_all(blend)', lambda v: union_all([sph, move(bx, (3, 0, 0))], blend=v), 1.0)
check('intersection_all(blend)', lambda v: intersection_all([sph, bx], blend=v), 1.0)
check('repeat(spacing)', lambda v: repeat(sph, (v, v, v)), 12.0)
check('mirror_x(x)', lambda v: mirror_x(move(bx, (2, 0, 0)), v), 1.0)
check('twist_z(rate)', lambda v: twist_z(bx, v), 2.0)
check('bend_z(radius)', lambda v: bend_z(bx, v), 30.0)
check('noise_field(scale)', lambda v: noise_field(scale=v), 6.0)
check('noise_field(amplitude)', lambda v: noise_field(amplitude=v), 2.0)
check('distance_to_point(coordinate)', lambda v: distance_to_point((v, 0, 0)), 1.0)
check('distance_to_plane(point)', lambda v: distance_to_plane((0, 0, v)), 1.0)
check('mass_properties(density)', lambda v: Shape.wrap(mass_properties(sph, density=v, lower=(-6, -6, -6), upper=(6, 6, 6), resolution=2)['mass']), 1.0)

# --- surfaces
check('ellipsoid(radii)', lambda v: ellipsoid((v, 4.0, 3.0)), 5.0)
check('elliptic_cylinder(a, b)', lambda v: elliptic_cylinder(v, 3.0), 5.0)
check('hyperboloid(a, c)', lambda v: hyperboloid(v, 2.0), 3.0)
check('paraboloid(k)', lambda v: paraboloid(v), 0.2)
check('wave_block(amplitude)', lambda v: wave_block((10, 10, 6), v, 5.0), 1.0)
check('wave_block(period)', lambda v: wave_block((10, 10, 6), 1.0, v), 5.0)
check('coil_spring(wire_radius)', lambda v: coil_spring(4.0, v, 3.0, 3), 0.5)
check('coil_spring(lead)', lambda v: coil_spring(4.0, 0.5, v, 3), 3.0)

# --- lattices
body = box_exact((-8, -8, -8), (8, 8, 8))
check('lattice(thickness) gyroid', lambda v: lattice(body, cell_periodic('gyroid'), cell_size=8, thickness=v), 1.0)
check('lattice(radius) strut', lambda v: lattice(body, cell_periodic('octet'), cell_size=8, radius=v), 0.5)
check('lattice(density) gyroid', lambda v: lattice(body, cell_periodic('gyroid'), cell_size=8, density=v), 0.3)
check('lattice(skin)', lambda v: lattice(body, cell_periodic('gyroid'), cell_size=8, thickness=1.0, skin=v), 1.0)
check('lattice(depth, shell)', lambda v: lattice(body, cell_periodic('gyroid'), cell_size=8, thickness=1.0, region='shell', depth=v), 3.0)
check('lattice(cell_size)', lambda v: lattice(body, cell_periodic('gyroid'), cell_size=v, thickness=1.0), 8.0)
check('lattice(blend) strut', lambda v: lattice(body, cell_periodic('octet'), cell_size=8, radius=0.5, blend=v), 0.5)
check('lattice(node_radius) strut', lambda v: lattice(body, cell_periodic('octet'), cell_size=8, radius=0.5, node_radius=v), 0.9)
check('lattice(wall) planar', lambda v: lattice(body, cell_periodic('hexagon'), cell_size=8, wall=v), 0.8)
check('tpms(thickness)', lambda v: tpms(cell_periodic('gyroid'), 8, thickness=v), 1.0)
check('tpms(offset) network', lambda v: tpms(cell_periodic('gyroid'), 8, style='network', offset=v), 0.3)
check('strut_lattice(radius)', lambda v: strut_lattice(cell_periodic('octet'), 8, radius=v), 0.5)
check('fill(skin)', lambda v: fill(body, tpms(cell_periodic('gyroid'), 8, thickness=1.0), skin=v), 1.0)

width = max(len(l) for l, _ in rows)
for label, status in rows:
    print('%-*s  %s' % (width, label, status))
print('\n%d slots, %d not field-ready' % (len(rows), len(bad)))

# --- a graded lattice: the cells are the size the field says
import math
b8 = box_exact((-12, -12, -12), (12, 12, 12))
size_field = 6.0 + (x_field() + 12.0) * 0.25            # 6 mm at x = -12, 12 mm at x = +12
g = lattice(b8, cell_periodic('gyroid'), cell_size=size_field, thickness=0.8)
vals_g = evaluate(g, [(x, 0.3 * x, 0.2) for x in (-10, -5, 0, 5, 10)])
print('graded gyroid evaluates:', ['%.2f' % v for v in vals_g])
too_wide = 1.0 + (x_field() + 12.0) * 2.0                # 1 .. 49 mm: more than a factor of 16
try:
    lattice(b8, cell_periodic('gyroid'), cell_size=too_wide, thickness=0.8)
    print('FAIL: a factor of 49 in cell size was accepted')
except ValueError as e:
    print('refused a factor of 49:', str(e)[:80])
