'''
Symmetry: `symmetry_planes(...)`, a model that is a set of mirror planes, and `symmetry_image(shape, symmetry, index)`, the image of a shape in it.

    mirror = symmetry_planes()                        # one plane, through the origin, the plane x = 0: a mirror
    mirror = gizmo(mirror, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)))        # moved and turned with its gizmo, like any model
    plate = box((5, 2, 0), (25, 12, 8))
    plate_2 = symmetry_image(plate, mirror, 1)        # the image of plate in the mirror: left to right

A symmetry is made of planes through one centre.  `kind` says how they are arranged round the axis (the line through the centre that all of them contain, or that is across the
plane of the last):

    'axis'      `planes` planes that all contain the axis, equally spaced (1: a mirror; 2: two at right angles; 3: 60 degrees apart ...)
    'across'    the same, and the plane across the axis (a part is mirrored top to bottom as well)
    'cube'      the nine planes of a cube whose axes are the three lines of the symmetry: three across them, six through its edges

Every plane makes an image of what is made while the symmetry is on, and so does every turn between them (two planes at right angles make a turn of half a way round): the
images of a symmetry are numbered, `symmetry_image(x, mirror, 1)` .. `symmetry_image(x, mirror, symmetry_images(mirror))`.  An image FOLLOWS what it is the image of
and the symmetry: move the original and the image moves the way a mirror shows; move the symmetry (its gizmo) and every image of it moves with it.  An image has no numbers of
its own: pulling it pulls the original.  FielDes writes the images of what you make while a symmetry is selected (Shift+S turns that on and off).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['symmetry_planes', 'symmetry_image', 'symmetry_images', 'SymmetryPlanes']

KINDS = ('axis', 'across', 'cube')

_PERMUTATIONS = ((0, 1, 2), (0, 2, 1), (1, 0, 2), (1, 2, 0), (2, 0, 1), (2, 1, 0))

# What an image has of the original: what the original IS and is called, not where it is (its gizmo, its outline, its box, its lines of points are placed by the symmetry)
_TAKEN = ('_step_metrics', '_part_name', '_step_ref', '_kind', '_smooth', 'size')


# ---- small matrices of plain numbers

def _unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _transpose(m):
    return [[m[j][i] for j in range(3)] for i in range(3)]


def _reflection(n):
    n = _unit(n)
    return [[(1.0 if i == j else 0.0) - 2.0 * n[i] * n[j] for j in range(3)] for i in range(3)]


def _rotation(axis, degrees):
    x, y, z = _unit(axis)
    t = math.radians(degrees)
    c, s = math.cos(t), math.sin(t)
    k = 1.0 - c
    return [[c + x * x * k, x * y * k - z * s, x * z * k + y * s],
            [y * x * k + z * s, c + y * y * k, y * z * k - x * s],
            [z * x * k - y * s, z * y * k + x * s, c + z * z * k]]


def _across(axis, hint):
    ''' A unit line across `axis`, as near `hint` as can be (any when `hint` is along it): the line of the first plane '''
    c = _unit(axis)
    if hint is not None:
        h = tuple(hint[i] - _dot(hint, c) * c[i] for i in range(3))
        if math.sqrt(_dot(h, h)) > 1e-3:
            return _unit(h)
    world = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    best = min(world, key=lambda w: abs(_dot(w, c)))
    return _unit(tuple(best[i] - _dot(best, c) * c[i] for i in range(3)))


def base_elements(definition):
    ''' The images of a symmetry as plain matrices (rows), in the order they are numbered: every turn and mirror image but the identity, in the symmetry's own place -- before its
        gizmo moved and turned it.  The same list, in the same order, that FielDes's viewport makes '''
    kind, planes, _centre, axis, first = definition
    c = _unit(axis)
    a = _across(c, first)
    b = _cross(c, a)
    if kind == 'cube':
        columns = [[a[i], b[i], c[i]] for i in range(3)]
        out = []
        for p in _PERMUTATIONS:
            for signs in range(8):
                if p == (0, 1, 2) and signs == 0:
                    continue
                s = [[0.0] * 3 for _ in range(3)]
                for i in range(3):
                    s[i][p[i]] = -1.0 if (signs >> i) & 1 else 1.0
                out.append(_matmul(columns, _matmul(s, _transpose(columns))))
        return out
    n = max(1, min(8, int(planes)))
    turns = [_rotation(c, 360.0 * k / n) for k in range(1, n)]
    mirrors = []
    for j in range(n):
        t = math.pi * j / n
        d = tuple(a[i] * math.cos(t) + b[i] * math.sin(t) for i in range(3))
        mirrors.append(_reflection(_cross(c, d)))
    out = turns + mirrors
    if kind == 'across':
        h = _reflection(c)
        out.append(h)
        out += [_matmul(h, t) for t in turns]
        out += [_matmul(h, m) for m in mirrors]
    return out


# ---- numbers that may be fields (the var()s of a gizmo): products and sums that keep the trees small

def _num(x):
    return not isinstance(x, Shape)


def _mul(a, b):
    if _num(a) and _num(b):
        return a * b
    if (_num(a) and a == 0.0) or (_num(b) and b == 0.0):
        return 0.0
    if _num(a) and a == 1.0:
        return b
    if _num(b) and b == 1.0:
        return a
    return a * b


def _add(a, b):
    if _num(a) and a == 0.0:
        return b
    if _num(b) and b == 0.0:
        return a
    return a + b


def _matmul_expr(a, b):
    out = []
    for i in range(3):
        row = []
        for j in range(3):
            acc = 0.0
            for k in range(3):
                acc = _add(acc, _mul(a[i][k], b[k][j]))
            row.append(acc)
        out.append(row)
    return out


def _turn_matrix(rotate):
    ''' What a gizmo's angles (degrees, x then y then z: numbers or var()s) turn by -- the matrices of rotate_x, rotate_y, rotate_z: Rz * Ry * Rx '''
    def cs(angle):
        if isinstance(angle, Shape):
            r = angle * (math.pi / 180.0)
            return r.cos(), r.sin()
        t = math.radians(float(angle))
        return math.cos(t), math.sin(t)
    (cx, sx), (cy, sy), (cz, sz) = cs(rotate[0]), cs(rotate[1]), cs(rotate[2])
    neg = lambda v: -v
    rx = [[1.0, 0.0, 0.0], [0.0, cx, neg(sx)], [0.0, sx, cx]]
    ry = [[cy, 0.0, neg(sy)], [0.0, 1.0, 0.0], [sy, 0.0, cy]]
    rz = [[cz, neg(sz), 0.0], [sz, cz, 0.0], [0.0, 0.0, 1.0]]
    return _matmul_expr(rz, _matmul_expr(ry, rx))


def _frame_of(sym):
    ''' Where a symmetry is now: its centre (numbers, or fields of the var()s of its gizmo) and the turn its gizmo makes (a matrix) '''
    kind, planes, centre, axis, first = sym._symmetry_def
    g = getattr(sym, '_gizmo', None)
    if g is None:
        return tuple(centre), [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    _mode, about, move, rotate, _scale = g
    return tuple(_add(float(about[i]), move[i]) for i in range(3)), _turn_matrix(rotate)


def symmetry_images(symmetry):
    ''' How many images a symmetry makes of what is made with it: `symmetry_image(x, symmetry, 1)` .. `symmetry_image(x, symmetry, symmetry_images(symmetry))` '''
    if getattr(symmetry, '_symmetry_def', None) is None:
        raise TypeError('symmetry_images(): a symmetry is made with symmetry_planes(...)')
    return len(base_elements(symmetry._symmetry_def))


class SymmetryPlanes(Shape):
    ''' A symmetry: mirror planes through a centre (see the top of this file).  In the model tree it is a model of its own kind; selected it has a gizmo that moves and turns it,
        and the symmetry card; nothing is drawn of it but its planes '''
    _kind = 'symmetry'

    def __init__(self, planes=1, kind='axis', centre=(0.0, 0.0, 0.0), axis=(0.0, 0.0, 1.0), first=None):
        if kind not in KINDS:
            raise ValueError("symmetry_planes(): kind is one of %s, not %r" % (', '.join(repr(k) for k in KINDS), kind))
        try:
            n = int(planes)
        except (TypeError, ValueError):
            raise TypeError('symmetry_planes(): planes is how many planes there are about the axis, 1 to 8')
        if not 1 <= n <= 8:
            raise ValueError('symmetry_planes(): planes is 1 to 8 (the planes about the axis, equally spaced)')
        centre = tuple(float(c) for c in centre)
        if len(centre) != 3:
            raise TypeError('symmetry_planes(): centre is three numbers')
        axis = tuple(float(c) for c in axis)
        if len(axis) != 3 or math.sqrt(_dot(axis, axis)) < 1e-9:
            raise ValueError('symmetry_planes(): axis is a direction (three numbers, not all 0)')
        axis = _unit(axis)
        a = _across(axis, (0.0, 1.0, 0.0) if first is None else tuple(float(c) for c in first))
        self._symmetry_def = (kind, n, centre, axis, a)
        self._kind = 'symmetry'
        self._no_handles = True
        self.xyz = centre                          # (where it is: its gizmo moves it as it moves a point)
        self._point_pos = centre
        from fieldes.stdlib.points import _view
        res, lo, hi = _view()
        radius = max(max(hi[i] - lo[i] for i in range(3)) / 120.0, 2.0 / res)
        x, y, z = (Shape.wrap(c) for c in centre)
        dx, dy, dz = Shape.X() - x, Shape.Y() - y, Shape.Z() - z
        ball = (dx.square() + dy.square() + dz.square()).sqrt() - radius
        super().__init__(lib.libfive_tree_copy(ball.ptr))
        self._bounds = (tuple(c - radius for c in centre), tuple(c + radius for c in centre))

    def __del__(self):
        # (a symmetry that was refused -- its numbers were wrong -- has no tree to free)
        if 'ptr' in self.__dict__:
            super().__del__()

    @property
    def images(self):
        return len(base_elements(self._symmetry_def))

    def __repr__(self):
        kind, planes, centre, axis, a = self._symmetry_def
        return 'Symmetry(%s, planes=%d, %d images)' % (kind, planes, self.images)


def symmetry_planes(planes=1, kind='axis', centre=(0, 0, 0), axis=(0, 0, 1), first=None):
    ''' A symmetry: mirror planes through `centre`.  `kind` is 'axis' (`planes` planes that all contain the axis `axis`, equally spaced: one is a mirror), 'across' (and the plane
        across the axis) or 'cube' (the nine planes of a cube).  `first` is a line in the first plane (across the axis; any when it is left out): with one plane the plane is the one
        that holds the axis and `first`.  The default is the mirror x = 0.

        It is a model: selected in the model tree it has a gizmo -- moved, turned -- and everything made while it is selected is made with its images (see symmetry_image) '''
    return SymmetryPlanes(planes, kind, centre, axis, first)


def _check(shape, sym, who):
    if not isinstance(shape, Shape):
        raise TypeError('%s(): only a shape can have an image (a body, a 2D shape, a surface, a point, a field ...), not %s' % (who, type(shape).__name__))
    if getattr(sym, '_symmetry_def', None) is None:
        raise TypeError('%s(): the second argument is a symmetry, made with symmetry_planes(...)' % who)


def symmetry_image(shape, symmetry, index=1):
    ''' The image of `shape` in the symmetry `symmetry`: `index` is the number of the image (1 .. symmetry_images(symmetry)).  It follows both: change the shape, or move the
        symmetry, and the image changes with it.  It has no gizmo and no numbers of its own: pulling it pulls the shape.  FielDes writes it for everything that is made while the
        symmetry is selected (Shift+S) '''
    sym = symmetry
    _check(shape, sym, 'symmetry_image')
    if getattr(shape, '_ctrl_pos', None):
        raise TypeError('symmetry_image(): a sketch or a curve is made of its points -- make the images of the points, and the sketch through them')
    elements = base_elements(sym._symmetry_def)
    try:
        k = int(index)
    except (TypeError, ValueError):
        raise TypeError('symmetry_image(): the number of the image is a whole number, 1 to %d' % len(elements))
    if not 1 <= k <= len(elements):
        raise ValueError('symmetry_image(): this symmetry has %d images -- number them 1 to %d, not %s' % (len(elements), len(elements), index))
    c, turn = _frame_of(sym)
    # (the image's matrix in the world: the element, in the symmetry's own axes, taken to where its gizmo has put them)
    m = _matmul_expr(turn, _matmul_expr(elements[k - 1], _transpose(turn)))
    X, Y, Z = Shape.X(), Shape.Y(), Shape.Z()
    d = [_add(V, _mul(-1.0, c[i])) for i, V in enumerate((X, Y, Z))]
    # A place of the image is the place of the shape that the way back -- the transpose, the matrix is a turn or a mirror image -- takes it from
    pre = tuple(_add(c[i], _add(_add(_mul(m[0][i], d[0]), _mul(m[1][i], d[1])), _mul(m[2][i], d[2]))) for i in range(3))
    moved = shape.remap(*pre)
    out = type(shape).__new__(type(shape))
    Shape.__init__(out, lib.libfive_tree_copy(moved.ptr))
    for name in _TAKEN:
        if name in shape.__dict__:
            out.__dict__[name] = shape.__dict__[name]
    from fieldes.stdlib.curves import value_of
    mn = [[value_of(m[i][j]) for j in range(3)] for i in range(3)]
    cn = tuple(value_of(v) for v in c)
    if hasattr(shape, 'xyz'):
        # A point is the point its image is: its coordinates follow the original's (numbers, or the expressions of its var()s)
        from fieldes.stdlib.points import Point
        p = tuple(shape.xyz)
        q = tuple(_add(c[i], _add(_add(_mul(m[i][0], _add(p[0], _mul(-1.0, c[0]))), _mul(m[i][1], _add(p[1], _mul(-1.0, c[1])))),
                                  _mul(m[i][2], _add(p[2], _mul(-1.0, c[2]))))) for i in range(3))
        out = Point(q[0], q[1], q[2], smooth=getattr(shape, '_smooth', None))
        out._kind = getattr(shape, '_kind', 'point')
    box = getattr(shape, '_bounds', None)
    if box and not hasattr(shape, 'xyz'):
        try:
            corners = []
            for x in (box[0][0], box[1][0]):
                for y in (box[0][1], box[1][1]):
                    for z in (box[0][2], box[1][2]):
                        p = (x, y, z)
                        corners.append(tuple(cn[i] + sum(mn[i][j] * (p[j] - cn[j]) for j in range(3)) for i in range(3)))
            out._bounds = (tuple(min(q[i] for q in corners) for i in range(3)), tuple(max(q[i] for q in corners) for i in range(3)))
            if getattr(shape, '_bounds_loose', False):
                out._bounds_loose = True
        except (TypeError, ValueError):
            pass
    # Its gizmo is the original's -- the numbers of the original's, so that pulling it pulls them -- and the viewport places it where the image is: `_symmetry` is the map (the
    # matrix, then the centre) that takes a place of the original to the image's, and a pull of the image back to a pull of the original
    g = getattr(shape, '_gizmo', None)
    if g is not None:
        out._gizmo = g
        out._symmetry = (tuple(v for row in mn for v in row), cn)
    out._no_handles = True
    out._symmetry_of = shape
    out._symmetry_source = sym
    out._symmetry_index = k
    return out
