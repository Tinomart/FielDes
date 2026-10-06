# Low-level fields: a body is a function of x, y and z.
#
# Underneath every body, field and lattice of FielDes is the functional representation of the libfive kernel: a tree of
# arithmetic over three coordinates, negative inside, zero on the surface, positive outside.  libfive documents it in Scheme
# (define-shape, remap-shape); here it is the same in Python.  `Shape.X()`, `Shape.Y()` and `Shape.Z()` are the coordinates, the
# operators + - * / ** and the methods .min .max .abs .sqrt .square .sin .cos .tan .atan2 .exp .log build the tree, and
# `.remap(x', y', z')` asks a shape about other coordinates (every transform is one).  Five things made that way, side by side:
#
#   1. a cube, as the largest of six plane distances            (the Scheme define-shape example)
#   2. the cube twisted about z with a remap                    (the Scheme remap-shape example)
#   3. a ball and a torus, from their formulas, joined by a smooth minimum of our own
#   4. a gyroid lattice, cut to a cube by a max
#   5. a shape as a function you call, and a tree you can print -- and a slot cut in a twisted cube by the library's own difference
#
# No STEP file is needed.  What comes out is an ordinary body: the library's own operations (union, offset, move, the
# analyses ...) take it as they take a box or a sphere.  Change a number and look.
from fieldes import *
from fieldes.shape import shape       # shape(f) calls f(x, y, z) with the three coordinates as trees, and gives the tree

view.set_bounds([-12, -12, -14], [124, 12, 14])
view.set_resolution(6)
view.set_quality(8)


# Python's own max and min cannot compare trees: these fold the methods .max and .min over their arguments, as the Scheme
# (max a b c ...) does
def maximum(*terms):
    out = terms[0]
    for t in terms[1:]:
        out = out.max(t)
    return out


def minimum(*terms):
    out = terms[0]
    for t in terms[1:]:
        out = out.min(t)
    return out


# 1. (define-shape (cube x y z) (max (- x 1) (- -1 x) (- y 1) (- -1 y) (- z 1) (- -1 z))), with a half-width of 6 mm.
#    Each term is the distance to one face's plane (positive outside it); the largest is positive when outside any of them.
def cube_of(x, y, z):
    return maximum(x - 6, -6 - x, y - 6, -6 - y, z - 6, -6 - z)


cube = shape(cube_of)
cube


# 2. (remap-shape (cube x y z) (+ (* (cos z) x) (* (sin z) y)) (- (* (cos z) y) (* (sin z) x)) z): every point asks the cube
#    about the point turned about z by an angle that grows with z, so the cube comes out twisted.  A shape is moved by a
#    remap too (asking about x - 28 puts it 28 mm along), which is all `move` is.
def twisted_by(s, radians_per_mm):
    x, y, z = Shape.X(), Shape.Y(), Shape.Z()
    turn = z * radians_per_mm
    return s.remap(turn.cos() * x + turn.sin() * y,
                   turn.cos() * y - turn.sin() * x,
                   z)


def shifted(s, dx, dy=0, dz=0):
    x, y, z = Shape.X(), Shape.Y(), Shape.Z()
    return s.remap(x - dx, y - dy, z - dz)


twisted = shifted(twisted_by(cube, 0.14), 28)
twisted


# 3. A ball and a torus from their formulas, and a smooth minimum of our own: where the two are close, h is near 1 and the
#    result is pulled below the plain minimum by a fillet of up to k / 4.
def ball_of(x, y, z):
    return (x.square() + y.square() + z.square()).sqrt() - 7


def torus_of(x, y, z):
    ring = (x.square() + y.square()).sqrt() - 8          # (distance from the circle of radius 8 in the xy plane)
    return (ring.square() + z.square()).sqrt() - 3


def smooth_min(a, b, k):
    h = (k - (a - b).abs()).max(0) / k
    return a.min(b) - h.square() * k / 4


ball = shape(ball_of)
torus = shape(torus_of)
joined = shifted(smooth_min(ball, shifted(torus, 9), 6), 56)
joined


# 4. A gyroid, sin x cos y + sin y cos z + sin z cos x, is zero on a surface that winds through space; its absolute value
#    less than a number is a wall round that surface.  The max with the cube keeps the part of it inside the cube.
def gyroid_of(x, y, z):
    k = 0.7                                              # (radians per mm: a cell is 2 pi / k = 9 mm)
    g = (k * x).sin() * (k * y).cos() + (k * y).sin() * (k * z).cos() + (k * z).sin() * (k * x).cos()
    return maximum(g.abs() - 0.45, cube_of(x, y, z))


gyroid = shifted(shape(gyroid_of), 84)
gyroid


# 5. A shape is a function you can call (a number at a point: negative inside), and a tree you can print.  The cube is -6 at
#    its middle, 0 on a face and 4 mm outside it; the gyroid is the Scheme-like text of its tree.
print('cube at (0, 0, 0), (6, 0, 0), (10, 0, 0):', cube(0, 0, 0), cube(6, 0, 0), cube(10, 0, 0))
print('a tree:', shape(lambda x, y, z: (x * y + 1).sqrt() - z))

# ...and what comes out of all this is a body like any other: here a twisted cube with a slot cut through it by the library's own
# box_exact and difference.  (A shape made from a max is not an exact distance: an offset of it is only about as large as it says.)
slotted = difference(shifted(twisted_by(cube, 0.14), 112), box_exact((108, -1.5, -10), (116, 1.5, 10)))
slotted
