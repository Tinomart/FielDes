# pressure(region, value): a load at right angles to the surface (H222; the user, 2026-11-14: "Implement pressure then as a boundary condition ... It takes a surface always").
# Library checks, no window.  They need the kernel that has libfive_tetfea_add_pressure (a build after the 2026-11-14 pressure change).  Every check has its number written before it is run:
#   1. a bar pulled by 10 MPa on its end face: the stress is 10 MPa all along it and the end moves by sigma L / E (the held end keeps the bar from contracting a little: within 6 %)
#   2. the same pressed instead of pulled: the same movement the other way (the sign of the pressure: positive presses INTO the part)
#   3. the resultant on a CURVED surface is what a pressure makes of it: a hollow cylinder with an inner pressure p (pressing into the wall) has the hoop stress p (b^2 + a^2) / (b^2 - a^2) at its inner wall
#      (Lame, the standard solution of a thick-walled cylinder), read on a quarter of a long one with 10-node tetrahedra: within 8 %
#   4. a pressure on a region the part does not touch is refused or says so; a pressure in the supports slot is an error that says where it goes
import math
import sys
import time

from fieldes import *

failures = []
t_all = time.time()


def check(name, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + name + ('   ' + str(detail) if detail != '' else ''))
    if not ok:
        failures.append(name)


E = steel.E if hasattr(steel, 'E') else 210000.0

# 1 and 2: the bar
L, W = 100.0, 10.0
bar = box((L, W, W), position=(L / 2, W / 2, W / 2))
wall = box((2.0, 4 * W, 4 * W), position=(-1.0 + 0.01, W / 2, W / 2))
end = box((2.0, 4 * W, 4 * W), position=(L + 1.0 - 0.01, W / 2, W / 2))
exact = 10.0 * L / E
t0 = time.time()
pulled = static_analysis(bar, supports=[fixed(wall)], loads=[pressure(end, -10.0)], material=steel, element_size=2.5, cache=False)
print('bar pulled by 10 MPa: max displacement %.6f mm (sigma L / E = %.6f), max von Mises %.2f MPa, %d tetrahedra, %.1f s' % (pulled.max_displacement, exact, pulled.max_von_mises, pulled.elements, time.time() - t0))
check('1. the end of the bar pulled by 10 MPa moves by sigma L / E (%.6f mm)' % exact, abs(float(pulled.max_displacement) - exact) / exact < 0.06, '%.6f' % pulled.max_displacement)
check('1. the stress in the bar is 10 MPa (von Mises within 15 %: the held end disturbs it)', abs(float(pulled.max_von_mises) - 10.0) / 10.0 < 0.15, '%.2f' % pulled.max_von_mises)
pressed = static_analysis(bar, supports=[fixed(wall)], loads=[pressure(end, 10.0)], material=steel, element_size=2.5, cache=False)
check('2. pressed by 10 MPa instead: the same movement', abs(float(pressed.max_displacement) - float(pulled.max_displacement)) / exact < 0.01, '%.6f against %.6f' % (pressed.max_displacement, pulled.max_displacement))
# the end moves the other way: the field of the displacement along x at the middle of the end face
try:
    up, down = float(pulled.dx(L, W / 2, W / 2)), float(pressed.dx(L, W / 2, W / 2))
    check('2. the end moves away from the wall when pulled and toward it when pressed', up > 0 > down, '%+.6f and %+.6f' % (up, down))
except AttributeError:
    print('     (the result has no dx(x, y, z): the direction of the movement is not read)')

# 3: the thick-walled cylinder (Lame): quarter, along z, inner radius a, outer b, inner pressure p into the wall
a, b, H, p = 10.0, 20.0, 40.0, 10.0
tube = intersection(difference(cylinder(b, H, position=(0, 0, H / 2)), cylinder(a, H + 10, position=(0, 0, H / 2))), box((b + 5, b + 5, H), position=((b + 5) / 2, (b + 5) / 2, H / 2)))
inner = difference(cylinder(a + 1.5, H + 4, position=(0, 0, H / 2)), cylinder(a - 1.5, H + 8, position=(0, 0, H / 2)))      # a band round the inner wall
face_x0 = box((2.0, b + 10, H + 10), position=(-1.0 + 0.01, (b + 5) / 2, H / 2))
face_y0 = box((b + 10, 2.0, H + 10), position=((b + 5) / 2, -1.0 + 0.01, H / 2))
face_z0 = box((b + 10, b + 10, 2.0), position=((b + 5) / 2, (b + 5) / 2, -1.0 + 0.01))
supports = [fixed(face_x0, displacement=(0, inf, inf)), fixed(face_y0, displacement=(inf, 0, inf)), fixed(face_z0, displacement=(inf, inf, 0))]
hoop = p * (b * b + a * a) / (b * b - a * a)
t0 = time.time()
mesh = mesh_tet10(tube, 2.0)
r3 = static_analysis(mesh, supports=supports, loads=[pressure(inner, p)], material=steel, element='tet10', cache=False)
read = float(r3.syy(a + 0.05, 0.0, H / 2))
print('thick cylinder a = %g, b = %g, p = %g MPa: hoop stress at the inner wall %.2f MPa (Lame: %.2f MPa), %d elements, %.1f s' % (a, b, p, read, hoop, r3.elements, time.time() - t0))
check('3. the hoop stress at the inner wall of a thick cylinder under an inner pressure is Lame\'s (%.2f MPa, within 8 %%)' % hoop, abs(read - hoop) / hoop < 0.08, '%.2f' % read)

# 4: errors that say where it goes
try:
    static_analysis(bar, supports=[pressure(end, 1.0)], loads=[], material=steel, element_size=5.0, cache=False)
    check('4. a pressure in supports= is an error', False, 'no error')
except Exception as error:
    check('4. a pressure in supports= is an error that says it goes in loads=', 'loads' in str(error), str(error)[:100])

print('=' * 70)
print('{} check(s) failed'.format(len(failures)) if failures else 'ALL CHECKS PASSED', failures, '   total {:.1f} s'.format(time.time() - t_all))
sys.exit(1 if failures else 0)
