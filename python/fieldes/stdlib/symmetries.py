'''
Symmetry: `symmetry_planes(...)`, a model that is a set of mirror planes, and `symmetry_images(shape, symmetry)`, ALL the images of a shape in it, as one model.

    mirror = symmetry_planes()                        # one plane, through the origin, the plane x = 0: a mirror
    mirror = gizmo(mirror, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)))        # moved and turned with its gizmo, like any model
    leg = cylinder(5, 30, (40, 22, 0))                # the one you edit
    leg_images = symmetry_images(leg, mirror)         # all its images, one model: the leg as it looks in the mirror, left to right

The leg is the only model with numbers: the images are not models of their own, they are what the symmetry makes of the leg, so a symmetry of two planes at right angles makes three
images (a turn of half a way round and two mirror images) in ONE model, and a cube's nine planes make forty-seven in one -- not forty-seven rows and forty-seven lines.

A symmetry is made of planes through one centre.  `kind` says how they are arranged round the axis (the line through the centre that all of them contain, or that is across the
plane of the last):

    'axis'      `planes` planes that all contain the axis, equally spaced (1: a mirror; 2: two at right angles; 3: 60 degrees apart ...)
    'across'    the same, and the plane across the axis (a part is mirrored top to bottom as well)
    'cube'      the nine planes of a cube whose axes are the three lines of the symmetry: three across them, six through its edges
    'turn'      no planes: `copies` copies of what is made, equally spaced round the axis, each the one before it turned by 360 / copies degrees (3: three legs 120 degrees
                apart; 5, 7 ...: any number, 1 to 24) -- the rotational symmetry of a three-legged table, a fan, a gear

Every plane makes an image of what is made while the symmetry is on, and so does every turn between them (two planes at right angles make a turn of half a way round); a 'turn'
symmetry has the turns only: the
images of a symmetry are numbered 1 .. symmetry_count(symmetry).  `symmetry_images(x, mirror)` is all of them (`only=(1, 3)` some of them), `symmetry_image(x, mirror, 2)` one.
The images FOLLOW what they are the images of and the symmetry: move the original and they move the way a mirror shows; move the symmetry (its gizmo) and every image of it moves
with it.  They have no numbers of their own: pulling one pulls the original.  FielDes writes the images of what you make while a symmetry is selected (Shift+S turns that on and off):
one model, `leg_images`, for a body, a 2D shape or a point (a body or a 2D shape: the union of its images; a point: a set of points); what is
made of other models -- a path_extrude, a union -- gets its images the same way (`distinct=True` leaves out those that would be the model itself or another image); a surface or a field
gets one model for each image, `symmetry_image`; so does a sketch, a line or a spline: it is made of its points, which are the models, so each point has its images
(`symmetry_image(point, mirror, 3)`) and the stroke goes through them.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['symmetry_planes', 'symmetry_image', 'symmetry_images', 'symmetry_count', 'SymmetryPlanes']

KINDS = ('axis', 'across', 'cube', 'turn')
MAX_COPIES = 24                                  # (a 'turn' symmetry: how many copies there can be)

_PERMUTATIONS = ((0, 1, 2), (0, 2, 1), (1, 0, 2), (1, 2, 0), (2, 0, 1), (2, 1, 0))

# What an image has of the original: what the original IS and is called, not where it is (its gizmo, its outline, its box, its lines of points are placed by the symmetry)
_TAKEN = ('_step_metrics', '_part_name', '_step_ref', '_kind', '_smooth', '_radius', 'size')


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


def _determinant(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def _reflection(n):
    n = _unit(n)
    return [[(1.0 if i == j else 0.0) - 2.0 * n[i] * n[j] for j in range(3)] for i in range(3)]


def _exact(v):
    ''' A number of a turn or a mirror image without the rounding of the sines and cosines it came from: cos(90 degrees) is 6e-17, not 0.  A shape whose faces lie on the planes of the
        grid it is meshed on (a box, an extrusion, at whole millimetres) has its image's faces there too -- but the noise puts every lattice point of the face a hair to one side or the
        other, and the mesh grows teeth along it '''
    r = round(v, 12)
    return 0.0 if r == 0.0 else r


def _rotation(axis, degrees):
    x, y, z = _unit(axis)
    t = math.radians(degrees)
    c, s = math.cos(t), math.sin(t)
    k = 1.0 - c
    m = [[c + x * x * k, x * y * k - z * s, x * z * k + y * s],
         [y * x * k + z * s, c + y * y * k, y * z * k - x * s],
         [z * x * k - y * s, z * y * k + x * s, c + z * z * k]]
    return [[_exact(v) for v in row] for row in m]


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
    if kind == 'turn':
        n = max(1, min(MAX_COPIES, int(planes)))
        return [_rotation(c, 360.0 * k / n) for k in range(1, n)]
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


def symmetry_count(symmetry):
    ''' How many images a symmetry makes of what is made with it: they are numbered 1 .. symmetry_count(symmetry) (see symmetry_image) '''
    if getattr(symmetry, '_symmetry_def', None) is None:
        raise TypeError('symmetry_count(): a symmetry is made with symmetry_planes(...)')
    return len(base_elements(symmetry._symmetry_def))


class SymmetryPlanes(Shape):
    ''' A symmetry: mirror planes through a centre (see the top of this file).  In the model tree it is a model of its own kind; selected it has a gizmo that moves and turns it,
        and the symmetry card; nothing is drawn of it but its planes '''
    _kind = 'symmetry'

    def __init__(self, planes=1, kind='axis', centre=(0.0, 0.0, 0.0), axis=(0.0, 0.0, 1.0), first=None, copies=None):
        if kind not in KINDS:
            raise ValueError("symmetry_planes(): kind is one of %s, not %r" % (', '.join(repr(k) for k in KINDS), kind))
        if kind == 'turn':
            # (no planes: the number is how many copies there are, round the axis)
            try:
                n = int(3 if copies is None else copies)
            except (TypeError, ValueError):
                raise TypeError("symmetry_planes(): copies is how many copies there are round the axis, 1 to %d" % MAX_COPIES)
            if not 1 <= n <= MAX_COPIES:
                raise ValueError("symmetry_planes(): copies is 1 to %d (the copies round the axis, equally spaced)" % MAX_COPIES)
        else:
            if copies is not None:
                raise ValueError("symmetry_planes(): copies= is for kind='turn' (a symmetry of copies turned about the axis); the planes of kind='%s' are planes=" % kind)
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
        return 'Symmetry(%s, %s=%d, %d images)' % (kind, 'copies' if kind == 'turn' else 'planes', planes, self.images)


def symmetry_planes(planes=1, kind='axis', centre=(0, 0, 0), axis=(0, 0, 1), first=None, copies=None):
    ''' A symmetry: mirror planes through `centre`.  `kind` is 'axis' (`planes` planes that all contain the axis `axis`, equally spaced: one is a mirror), 'across' (and the plane
        across the axis) or 'cube' (the nine planes of a cube).  `first` is a line in the first plane (across the axis; any when it is left out): with one plane the plane is the one
        that holds the axis and `first`.  The default is the mirror x = 0.

        `kind='turn'` has no planes: it is the rotational symmetry of `copies` copies equally spaced round the axis (`copies=3`: three legs 120 degrees apart; 5, 7 ... up to 24),
        each the one before turned by 360 / copies degrees -- nothing is mirrored.

        It is a model: selected in the model tree it has a gizmo -- moved, turned -- and everything made while it is selected is made with its images (see symmetry_image) '''
    return SymmetryPlanes(planes, kind, centre, axis, first, copies)


def _check(shape, sym, who):
    if not isinstance(shape, Shape):
        raise TypeError('%s(): only a shape can have an image (a body, a 2D shape, a surface, a point, a field ...), not %s' % (who, type(shape).__name__))
    if getattr(sym, '_symmetry_def', None) is None:
        raise TypeError('%s(): the second argument is a symmetry, made with symmetry_planes(...)' % who)


def _native_sweep(shape):
    ''' Whether the image of this sweep can be drawn natively, from the numbers of its profile and its path, as the sweep is: it has them, no point of either is turned, and its gizmo
        places it by a move only (a turn or a scale of it is a number that must stay what it is: see the guard) '''
    from fieldes.stdlib.curves import value_of

    def number(v):
        return value_of(v) if isinstance(v, Shape) else float(v)
    if getattr(shape, '_sweep_ppos', None) is None or getattr(shape, '_sweep_qpos', None) is None:
        return False
    for rotations in (getattr(shape, '_sweep_prot', None), getattr(shape, '_sweep_qrot', None)):
        for r in rotations or []:
            if r is not None and any(abs(number(c)) > 1e-9 for c in r):
                return False
    return True


def _map_place(p, mn, cn):
    ''' The place `p` (three numbers, or expressions of var()s) taken through the turn or mirror image: c + M (p - c) '''
    d = [_add(p[j], -cn[j]) for j in range(3)]
    return tuple(_add(cn[i], _add(_add(_mul(mn[i][0], d[0]), _mul(mn[i][1], d[1])), _mul(mn[i][2], d[2]))) for i in range(3))


def _sweep_image(out, shape, mn, cn):
    ''' Gives `out`, the image of a sweep, what the viewport draws a sweep from: the outline of its profile and the points of its path, taken through the turn or mirror image
        (expressions of the original's numbers, so that it follows a point of the path as the sweep does, and nothing is meshed), the twist turned the other way for a mirror
        image, and its gizmo's move turned with it.  A mirror image turns the outline the way round: its points are taken in reverse, so that it faces as the original does '''
    from fieldes.stdlib.curves import value_of

    def number(v):
        return value_of(v) if isinstance(v, Shape) else float(v)
    improper = _determinant(mn) < 0
    ring = [_map_place(p, mn, cn) for p in shape._sweep_ppos]
    rot, flag, scale = list(shape._sweep_prot), list(shape._sweep_pflag), list(shape._sweep_pscale)
    radius = list(getattr(shape, '_sweep_pradius', None) or [None] * len(ring))
    if improper:
        ring.reverse()
        rot.reverse()
        flag.reverse()
        scale.reverse()
        radius.reverse()
    out._sweep_ppos, out._sweep_prot, out._sweep_pflag, out._sweep_pscale = ring, rot, flag, scale
    out._sweep_pradius = radius
    out._sweep_pdefault = shape._sweep_pdefault
    out._sweep_qpos = [_map_place(p, mn, cn) for p in shape._sweep_qpos]
    out._sweep_qrot, out._sweep_qflag, out._sweep_qscale = shape._sweep_qrot, shape._sweep_qflag, shape._sweep_qscale
    out._sweep_qdefault, out._sweep_qclosed = shape._sweep_qdefault, shape._sweep_qclosed
    out._sweep_twist = Shape.wrap(shape._sweep_twist) * (-1.0 if improper else 1.0)
    out._sweep_free = False                      # (its outline is given in space: the plane is the one its points make)
    guard = list(getattr(shape, '_sweep_guard', []))
    place = getattr(shape, '_sweep_place', None)
    if place is not None:
        about, move, rotate, scale_ = place
        guard += [(v, 0.0) for v in rotate if isinstance(v, Shape)] + [(v, 1.0) for v in scale_ if isinstance(v, Shape)]
        if any(abs(number(v)) > 1e-9 for v in rotate) or any(abs(number(v) - 1.0) > 1e-9 for v in scale_):
            return False                         # (it is turned or scaled by its gizmo now: drawn from its field)
        turned = tuple(_add(_add(_mul(mn[i][0], move[0]), _mul(mn[i][1], move[1])), _mul(mn[i][2], move[2])) for i in range(3))
        out._sweep_place = (_map_place(about, mn, cn), tuple(Shape.wrap(v) for v in turned), tuple(Shape.wrap(0.0) for _ in range(3)), tuple(Shape.wrap(1.0) for _ in range(3)))
    out._sweep_guard = guard
    out._drawn_key = getattr(shape, '_drawn_key', None)
    return True


def symmetry_image(shape, symmetry, index=1):
    ''' The image of `shape` in the symmetry `symmetry`: `index` is the number of the image (1 .. symmetry_count(symmetry)).  It follows both: change the shape, or move the
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
    from fieldes.stdlib.curves import value_of
    mn = [[value_of(m[i][j]) for j in range(3)] for i in range(3)]
    cn = tuple(value_of(v) for v in c)
    # A sweep is drawn natively from the numbers of its path (its field has none of them: it is made of numbers), so its image is drawn the same way, and its field is the original's
    # taken through the turn or mirror image AS IT IS NOW -- a field of thousands of terms, with the symmetry's numbers in every one of them, was minutes to mesh and seconds to key
    native = _native_sweep(shape)
    if native:
        dn = [_add(V, -cn[i]) for i, V in enumerate((X, Y, Z))]
        pre = tuple(_add(cn[i], _add(_add(_mul(mn[0][i], dn[0]), _mul(mn[1][i], dn[1])), _mul(mn[2][i], dn[2]))) for i in range(3))
    else:
        pre = tuple(_add(c[i], _add(_add(_mul(m[0][i], d[0]), _mul(m[1][i], d[1])), _mul(m[2][i], d[2]))) for i in range(3))
    moved = shape.remap(*pre)
    out = type(shape).__new__(type(shape))
    Shape.__init__(out, lib.libfive_tree_copy(moved.ptr))
    for name in _TAKEN:
        if name in shape.__dict__:
            out.__dict__[name] = shape.__dict__[name]
    if native:
        _sweep_image(out, shape, mn, cn)
    if hasattr(shape, 'xyz'):
        # A point is the point its image is: its coordinates follow the original's (numbers, or the expressions of its var()s)
        from fieldes.stdlib.points import Point
        p = tuple(shape.xyz)
        q = tuple(_add(c[i], _add(_add(_mul(m[i][0], _add(p[0], _mul(-1.0, c[0]))), _mul(m[i][1], _add(p[1], _mul(-1.0, c[1])))),
                                  _mul(m[i][2], _add(p[2], _mul(-1.0, c[2]))))) for i in range(3))
        out = Point(q[0], q[1], q[2], smooth=getattr(shape, '_smooth', None), radius=getattr(shape, '_radius', None))
        out._kind = getattr(shape, '_kind', 'point')
        out._symmetry_linear = mn            # (the turn or mirror image: what a shift the original is drawn with becomes -- see points.weld_shift_of)
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
    # The symmetry's own numbers (its gizmo's) are in the image's field, but a pull of the image's surface changes the original's numbers, never these: the viewport leaves them
    sg = getattr(sym, '_gizmo', None)
    if sg is not None:
        out._symmetry_vars = tuple(v for group in sg[2:5] if group for v in group if isinstance(v, Shape))
    out._symmetry_of = shape
    out._symmetry_source = sym
    out._symmetry_index = k
    return out


def _numeric_map(sym, k):
    ''' The image numbered `k` of the symmetry as it is now, in numbers: (the matrix, rows, and the centre it turns or mirrors about) -- the place the image takes `p` to is
        c + M (p - c), and the place its way back takes it from is c + Mt (p - c) '''
    from fieldes.stdlib.curves import value_of
    c, turn = _frame_of(sym)
    m = _matmul_expr(turn, _matmul_expr(base_elements(sym._symmetry_def)[k - 1], _transpose(turn)))
    return [[value_of(m[i][j]) for j in range(3)] for i in range(3)], tuple(value_of(v) for v in c)


def _distinct_by_key(shape, images):
    ''' `_distinct` for a shape that can say what it is drawn from (`_drawn_key`: a function of the shape that gives a key, equal for the shapes that are the same, or None): told by
        that and not by its field -- a sweep's field is thousands of terms (a script's var()s and all), and sampling it was seconds to minutes, or an error, where the numbers are
        told at once.  An image that cannot say is kept: nothing is left out that might be another shape '''
    first = shape._drawn_key(shape)
    seen, kept = [first], []
    for i, im in enumerate(images):
        key_of = getattr(im, '_drawn_key', None)
        key = key_of(im) if key_of is not None else None
        if key is not None and key in seen:
            continue
        seen.append(key)
        kept.append(i)
    return kept


def _distinct(shape, images, sym=None, numbers=None):
    ''' Which of the images are neither the shape itself nor another image, told by the field of each at a spread of places round them (the image of a model is the model's
        field at the places the way back takes it from, so two are the same shape where their fields are the same).  The numbers (0 ..) of those that are not.  With the symmetry
        and the numbers of the images (`sym`, `numbers`) the model's field is what is evaluated, once, at the places the way back of each image takes the sample from: the field of
        every image is not built for it -- a sweep's field is thousands of terms, and an image of it as many again '''
    import random
    if hasattr(shape, 'xyz'):
        def place(s):
            return getattr(s, '_point_pos', None)
        seen, kept = [place(shape)], []
        for i, im in enumerate(images):
            p = place(im)
            if p is not None and any(q is not None and math.dist(p, q) < 1e-6 * max(1.0, max(abs(c) for c in p)) for q in seen):
                continue
            seen.append(p)
            kept.append(i)
        return kept
    if getattr(shape, '_drawn_key', None) is not None:
        return _distinct_by_key(shape, images)
    from fieldes.stdlib.fields import evaluate
    from fieldes.stdlib.points import _view
    boxes = [getattr(s, '_bounds', None) for s in [shape] + list(images)]
    if all(boxes):
        lo = [min(b[0][i] for b in boxes) for i in range(3)]
        hi = [max(b[1][i] for b in boxes) for i in range(3)]
    else:
        _res, lo, hi = _view()
        lo, hi = [float(c) for c in lo], [float(c) for c in hi]
    pad = [0.1 * max(hi[i] - lo[i], 1e-6) for i in range(3)]
    rng = random.Random(7)
    pts = [tuple(rng.uniform(lo[i] - pad[i], hi[i] + pad[i]) for i in range(3)) for _ in range(120)]
    try:
        if sym is not None and numbers is not None:
            places = list(pts)
            for k in numbers:
                mn, cn = _numeric_map(sym, k)
                places += [tuple(cn[i] + sum(mn[j][i] * (p[j] - cn[j]) for j in range(3)) for i in range(3)) for p in pts]
            every = evaluate(shape, places)
            n = len(pts)
            base = every[:n]
            fields = [every[n * (i + 1):n * (i + 2)] for i in range(len(images))]
        else:
            base = evaluate(shape, pts)
            fields = None
        scale = max(1.0, max(abs(v) for v in base))
        kept, seen = [], [base]
        for i, im in enumerate(images):
            values = fields[i] if fields is not None else evaluate(im, pts)
            if any(max(abs(a - b) for a, b in zip(values, other)) < 2e-4 * scale for other in seen):
                continue
            seen.append(values)
            kept.append(i)
        return kept
    except Exception:
        return list(range(len(images)))


def _carried_parts(shape):
    ''' The parts of a model that have points of their own to drag -- the sketches, the lines and the points it was made from, found through everything it was made from (see
        made_from.py: `leg = path_extrude(leg_profile, leg_path)` was made from `leg_profile` and `leg_path`, and `leg = gizmo(leg, ...)` from the leg before it).  A part is a point,
        or a model with points that can make itself again through other points (`_remake`: a curve, a sketch); what it was made from is its points, not looked into.  The images
        of a model that is an image already are not looked into either: they bring their own '''
    from fieldes.shape import Shape
    parts, seen = [], set()

    def visit(node):
        if not isinstance(node, Shape) or id(node) in seen:
            return
        seen.add(id(node))
        if getattr(node, '_symmetry_of', None) is not None or getattr(node, '_symmetry_def', None) is not None:
            return
        points = getattr(node, 'points', None)
        if hasattr(node, 'xyz') or (isinstance(points, list) and points and hasattr(node, '_remake') and all(isinstance(p, Shape) and hasattr(p, 'xyz') for p in points)):
            parts.append(node)
            return
        for source in getattr(node, '_made_from', None) or ():
            visit(source)
    for source in getattr(shape, '_made_from', None) or ():
        visit(source)
    return parts


def _carried_images(shape, sym, numbers):
    ''' What the images of a model bring along: its parts (see `_carried_parts`) taken through each of the numbered images -- a line or a sketch through the images of its points,
        and a dot for each point.  A dot is the image of the point (see symmetry_image): pressed and dragged, it moves the ORIGINAL point, and every image follows.  Nothing of this is
        a model of its own: no statement, no row of the tree -- they are parts of the model, and the model and its parts are mirrored together '''
    parts = _carried_parts(shape)
    out = []
    for k in numbers:
        for part in parts:
            if hasattr(part, 'xyz'):
                dot = symmetry_image(part, sym, k)
                dot._gizmo_dot_only = True              # (it has no gizmo of its own to show: the original point has)
                out.append(dot)
                continue
            dots = [symmetry_image(p, sym, k) for p in part.points]
            for dot in dots:
                dot._gizmo_dot_only = True
            made = part._remake(dots, _determinant(dots[0]._symmetry_linear) < 0)
            made._editable = False                      # (a press on it puts no point on it: the original's is what has them)
            made._symmetry_of = part
            out.append(made)
            out.extend(dots)
    return out


def symmetry_images(shape, symmetry, only=None, distinct=False):
    ''' ALL the images of `shape` in the symmetry `symmetry`, as ONE model -- or those numbered in `only`, `only=(1, 3)`; with `distinct=True` those that are neither the shape
        itself nor another image (a tube along a path that lies in a plane of the symmetry has no image in that plane) -- so that what a symmetry of several planes makes of a leg
        is one row of the model tree and one line of the script, not one for each image.  It is the union of the images for a body or a 2D shape (the shape
        itself is not in it: it is its own model, the one you edit), and a set of points for a point.  The viewport draws each image as a shape of its own, meshed and moved on its
        own (so that dragging the shape moves its images as it is dragged, with nothing meshed again), all of them one model of the tree: select it and all of them are selected.
        It follows the shape and the symmetry as each image does (see symmetry_image) and has no numbers of its own: its gizmo is the shape's, drawn at the first image, and pulling
        a surface of any of them pulls the shape.

        What a shape was made from is mirrored TOGETHER with it: the sketches, the lines and the points of the models it was made from -- the sketch and the path of a
        `path_extrude`, the sketch of an `extrude`, whatever the operation -- are drawn with each image, as a copy of the line or the outline and a dot for each point.  They are not
        models -- there is no statement and no row of the model tree for them -- and the ones of the shape you made stay the only ones with numbers: press a dot of any image's and drag
        it, and it is that point of the original that moves.  '''
    sym = symmetry
    _check(shape, sym, 'symmetry_images')
    if getattr(shape, '_ctrl_pos', None):
        raise TypeError('symmetry_images(): a sketch or a line is made of its points, so each of its images is a model of its own -- symmetry_image(point, symmetry, 1), 2 ... for each point, and the sketch through them')
    count = len(base_elements(sym._symmetry_def))
    if only is None:
        numbers = list(range(1, count + 1))
    else:
        try:
            numbers = [int(k) for k in (only if isinstance(only, (tuple, list)) else (only,))]
        except (TypeError, ValueError):
            raise TypeError('symmetry_images(): only= is the numbers of the images, as (1, 3), each 1 to %d' % count)
        for k in numbers:
            if not 1 <= k <= count:
                raise ValueError('symmetry_images(): this symmetry has %d images -- number them 1 to %d, not %s' % (count, count, k))
    if not numbers:
        raise ValueError('symmetry_images(): only= leaves no image')
    images = [symmetry_image(shape, sym, k) for k in numbers]
    if distinct:
        keep = _distinct(shape, images, sym, numbers)
        numbers = [numbers[i] for i in keep]
        images = [images[i] for i in keep]
    for im in images:
        im._symmetry_of = shape
        im._symmetry_source = sym
    for im in images[1:]:
        # (the numbers are the shape's: one gizmo is enough -- the others only move with it, as the shape is dragged)
        if getattr(im, '_gizmo', None) is not None:
            im._gizmo_hidden = True

    extras = _carried_images(shape, sym, numbers)

    def shown():
        return list(images) + extras        # (each is drawn as its own kind of shape is: the interpreter asks every one for its display -- the images, and the path of a sweep with them)

    if not images:
        # (nothing is made of it that is not the model itself: a set of no shape)
        from fieldes.stdlib.shapes import emptiness
        out = emptiness()
        out._kind = getattr(shape, '_kind', None)
        out._display = lambda: []
    elif hasattr(shape, 'xyz'):
        # A point: its images are a set of points, where they are now (each is drawn as a point of its own: it follows the point as it is dragged)
        from fieldes.stdlib.lattices import PointSet
        places = [tuple(float(c) for c in im._point_pos) for im in images]
        nearest = min([math.dist(a, b) for i, a in enumerate(places) for b in places[i + 1:]] or [1.0])
        out = PointSet(places, nearest if nearest > 0 else 1.0)
        out._display = shown
    else:
        field = images[0]
        for im in images[1:]:
            field = field.min(im)                              # (the union of the images: the smaller of their fields)
        out = type(shape).__new__(type(shape))
        Shape.__init__(out, lib.libfive_tree_copy(field.ptr))
        for name in _TAKEN:
            if name in shape.__dict__:
                out.__dict__[name] = shape.__dict__[name]
        boxes = [getattr(im, '_bounds', None) for im in images]
        if all(boxes):
            out._bounds = (tuple(min(b[0][i] for b in boxes) for i in range(3)), tuple(max(b[1][i] for b in boxes) for i in range(3)))
            if any(getattr(im, '_bounds_loose', False) for im in images):
                out._bounds_loose = True
        out._no_gizmo = True                 # (its gizmo is the shape's: the first image has it)
        out._no_handles = True
        out._display = shown                 # (what is drawn: the images, each on its own; the union is what an operation made of this model works on)
    out._symmetry_of = shape
    out._symmetry_source = sym
    out._symmetry_set = True
    return out
