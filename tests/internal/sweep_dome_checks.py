''' The rounded ends of a path extrusion (the user: "Spline edges at the ends of path extrusions should have rounded ends"): the field of a circle swept along a path of
    spline points ends in a dome, along a path of corners in the flat cutoff.  A circle of radius 2 on three points on the z axis (0, 10, 20): the dome is 2 deep and at a
    distance s past the end plane the section is a circle of radius 2 sqrt(1 - (s / 2)^2) -- 1.732 at s = 1.  Run like the other internal checks (the library on the path, FIELDES_DIR set) '''
import sys

from fieldes import point, circle, spline, polyline, path_extrude
from fieldes.stdlib.curves import value_of

failures = []


def check(label, ok):
    print(('ok    ' if ok else 'FAIL  ') + label)
    if not ok:
        failures.append(label)


def field(shape, x, y, z):
    return value_of(shape(x, y, z))


points = [point(), point(position=(0, 0, 10)), point(position=(0, 0, 20))]
rounded = path_extrude(circle(2), spline(points))
flat = path_extrude(circle(2), polyline(points))

for name, tube in (('spline', rounded), ('polyline', flat)):
    check('%s path: inside on the axis in the middle' % name, field(tube, 0, 0, 10) < 0)
    check('%s path: inside at r 1.9 in the middle' % name, field(tube, 1.9, 0, 10) < 0)
    check('%s path: outside at r 2.1 in the middle' % name, field(tube, 2.1, 0, 10) > 0)
    check('%s path: inside at r 1.9 just before the end plane' % name, field(tube, 1.9, 0, 19.9) < 0)

# the dome: a circle of 1.732 at 1 past the end plane, either end; nothing past its depth
for label, z, tip in (('end', 21.0, 21.9), ('start', -1.0, -1.9)):
    check('spline path, %s: inside at r 1.7, 1 past the plane' % label, field(rounded, 1.7, 0, z) < 0)
    check('spline path, %s: outside at r 1.8, 1 past the plane' % label, field(rounded, 1.8, 0, z) > 0)
    check('spline path, %s: the surface is at r 1.732, 1 past the plane' % label, abs(field(rounded, 1.732, 0, z)) < 0.01)
    check('spline path, %s: inside just under the tip' % label, field(rounded, 0, 0, tip) < 0)
    check('spline path, %s: outside past the tip' % label, field(rounded, 0, 0, tip + (0.2 if z > 0 else -0.2)) > 0)

# the flat cutoff stays what it was
for label, z in (('end', 21.0), ('start', -1.0)):
    check('polyline path, %s: nothing past the flat cutoff' % label, field(flat, 0, 0, z) > 0 and field(flat, 1.7, 0, z) > 0)

print('%d checks failed' % len(failures) if failures else 'all passed')
sys.exit(1 if failures else 0)
