'''
Points and surfaces: models of their own kind, and how every kind is drawn.

    p = point(10, 0, 5)                    a point: drawn as a dot (the middle point of a gizmo); click it and its gizmo shows, and moves it
    s = plane((0, 0, 8), (0, 0, 1))        a surface: an open sheet, the zero set of a field, with no body behind it
    wavy = wave_surface(2, 10)             z = 2 sin(2 pi x / 10)

A point is usable where a field function takes a position (distance_to_point(p), attractor(p, ...)) and gives its
coordinates as p.xyz.  A surface is a field like any other (negative on one side, positive on the other), so it works
wherever a field does: thicken(s, 1) makes it a wall, lattice_surface_conform(s, ...) lays a lattice on it.

What is drawn for each kind of model (Shape._display calls displayed() below):
    a 3D shape and a simulation   as they are
    a 2D shape (no z)             its face in the z = 0 plane: flat, a sheet with no thickness (it has no height)
    a surface                     the sheet itself, with no thickness (the zero set of its field, meshed as it is)
    a point                       a dot, the middle point of a gizmo (click it to select the point: its gizmo shows)
    a field                       nothing in the viewport: selected in the model tree it is shown by the section viewer,
                                  which colours a plane through the render region by the field (move the plane to see
                                  the field in 3D)

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import numbers

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.kinds import kind_of

__all__ = ['Point', 'Surface', 'point', 'surface', 'plane', 'sphere_surface', 'cylinder_surface', 'wave_surface']


def _view():
    ''' (resolution, lower corner, upper corner) of the render region as the script has set them so far '''
    res, bounds = None, None
    try:
        import _fieldes_host as host
        res = getattr(host, '__resolution', None)
        bounds = getattr(host, '__bounds', None)
    except ImportError:
        pass
    res = float(res) if res else 5.0
    if bounds:
        lo, hi = tuple(bounds[0]), tuple(bounds[1])
    else:
        lo, hi = (-10.0, -10.0, -10.0), (10.0, 10.0, 10.0)
    return res, lo, hi


def weld_shift_of(shape):
    ''' The shift a point or a curve is DRAWN with: its own (path_extrude sets it on the path of a sweep and on its points), or -- for the image of such a point in a symmetry -- the
        original's, taken through the turn or mirror the image is made by.  The image of a path is drawn where the image of its sweep is, as the path is where its sweep is.
        None when it is drawn where it is '''
    shift = getattr(shape, '_weld_shift', None)
    if shift is not None:
        return shift
    source, linear = getattr(shape, '_symmetry_of', None), getattr(shape, '_symmetry_linear', None)
    if source is not None and linear is not None:
        s = weld_shift_of(source)
        if s is not None:
            return tuple(sum(linear[i][j] * s[j] for j in range(3)) for i in range(3))
    return None


def _weld_displayed(shape):
    ''' What is drawn for a point: itself, except a point of the path of a sweep of a profile (see path_extrude), which is drawn where the path is -- moved with it, its gizmo too --
        so that the path and its points run through the middle of the sweep.  Dragged, it is moved by what it is dragged by, as it is seen: only where it is drawn is shifted '''
    shift = weld_shift_of(shape)
    if shift is None:
        return shape
    out = Shape(lib.libfive_tree_copy(shape.ptr))
    for name, value in shape.__dict__.items():
        if name != 'ptr':
            out.__dict__[name] = value
    pos = getattr(shape, '_point_pos', None)
    if pos:
        out._point_pos = tuple(float(p) + s for p, s in zip(pos, shift))
    g = getattr(shape, '_gizmo', None)
    if g is not None:
        # (the gizmo of the image of a point is the ORIGINAL's, placed by the viewport through the turn or mirror image: its pivot is shifted by the original's shift -- shifted by the
        # image's, it would be taken through the mirror twice)
        source = getattr(shape, '_symmetry_of', None) if getattr(shape, '_symmetry', None) is not None else None
        gshift = (weld_shift_of(source) if source is not None else None) or shift
        out._gizmo = (g[0], tuple(float(a) + s for a, s in zip(g[1], gshift)), g[2], g[3], g[4])
    return out


def _as(source, tree):
    ''' A shape of the math of `tree` with the attributes of `source` (its handles, its name, its locks ... but not its kind) '''
    out = Shape(lib.libfive_tree_copy(tree.ptr))
    for name, value in source.__dict__.items():
        if name not in ('ptr', '_kind'):
            out.__dict__[name] = value
    return out


def _coord(c):
    return c if isinstance(c, Shape) else float(c)


def _position_of(xyz):
    ''' Where a point is, as three numbers, when that can be told: its coordinates are numbers, or fields that are the same everywhere (a
        var(), an expression of var()s) and have their value now.  None for a field that changes from place to place '''
    out = []
    for c in xyz:
        if isinstance(c, Shape):
            try:
                a, b = c(0.0, 0.0, 0.0), c(1.7, -2.3, 0.9)
            except Exception:
                return None
            if a != a or abs(a - b) > 1e-9 * max(1.0, abs(a)):
                return None
            out.append(float(a))
        else:
            out.append(float(c))
    return tuple(out)


class Point(Shape):
    ''' A point: p.xyz is its coordinates (numbers or fields).  The viewport draws it as **a dot** -- the middle point of a gizmo -- wherever it is;
        click the dot to select it, and its full gizmo shows.  (As a field it is a tiny ball, which gives it a place for its gizmo and for the
        bounds of what is made of it: nothing of the ball is painted.) '''
    _kind = 'point'
    _spline_point = False       # (set by a line or a profile that has the point as a spline point: its ring is its scale, Shift its influence -- see curves.Control and View::dragGizmo)

    def __init__(self, x=0.0, y=0.0, z=0.0, smooth=None, radius=None, influence=None):
        self.xyz = (_coord(x), _coord(y), _coord(z))
        self._kind = 'point'                    # (an instance attribute: it goes with the point through the placement and the like)
        self._smooth = None if smooth is None else bool(smooth)      # (what it is in a profile: a spline point or a corner; None: what the profile says)
        self._radius = radius                   # (what it is in a profile: a round corner of this radius -- a number or a var() -- or None: what the profile says)
        self._influence = influence             # (how far the point reaches -- a number or a var() -- or None: it does not say.  The radius of the field round it: distance_to_point, radial_field)
        self._point_pos = _position_of(self.xyz)    # (where the viewport draws its dot: the middle point of a gizmo, always there)
        res, lo, hi = _view()
        radius = max(max(hi[i] - lo[i] for i in range(3)) / 120.0, 2.0 / res)
        ball = self._ball(radius)               # (held: its tree is freed with it)
        super().__init__(lib.libfive_tree_copy(ball.ptr))
        if all(isinstance(c, float) for c in self.xyz):
            self._bounds = (tuple(c - radius for c in self.xyz), tuple(c + radius for c in self.xyz))

    def _ball(self, radius):
        x, y, z = (Shape.wrap(c) for c in self.xyz)
        dx, dy, dz = Shape.X() - x, Shape.Y() - y, Shape.Z() - z
        return (dx.square() + dy.square() + dz.square()).sqrt() - radius

    @property
    def x(self):
        return self.xyz[0]

    @property
    def y(self):
        return self.xyz[1]

    @property
    def z(self):
        return self.xyz[2]

    # A point is its position wherever a position goes (move(a, p), distance_to_line(p, d) ...): it reads as the three
    # coordinates it stands for.  (Arithmetic stays that of its field, the ball.)
    def __iter__(self):
        return iter(self.xyz)

    def __len__(self):
        return 3

    def __getitem__(self, i):
        return self.xyz[i]

    def __repr__(self):
        return 'Point({})'.format(', '.join(('<field>' if isinstance(c, Shape) else '%g' % c) for c in self.xyz))

    def _display(self):
        return _weld_displayed(self)


class Surface(Shape):
    ''' An open surface: the zero set of a field, with no body behind it (negative on one side, positive on the other) '''
    _kind = 'surface'

    def __init__(self, field):
        field = Shape.wrap(field)
        super().__init__(lib.libfive_tree_copy(field.ptr))
        self._kind = 'surface'                  # (an instance attribute: it goes with the surface through the placement and the like)

    def _display(self):
        # The field itself: the mesher finds where it is zero, and that is the sheet -- one layer of triangles, no thickness
        return _as(self, self)


def point(x=0.0, y=0.0, z=0.0, smooth=None, radius=None, influence=None):
    ''' A point at (x, y, z).  Its coordinates are numbers or fields.  It is drawn as a dot (the middle point of a gizmo): click it, and its gizmo shows.
        `influence` is how far the point reaches, in mm -- a number or a var(): the RADIUS of the field round it, when a point is given to `distance_to_point` or `radial_field`
        (the distance from the sphere, or the tube, of that radius: negative inside it, 0 on it, positive outside).  A point that does not say its influence is the point alone,
        and those fields are the plain distance from it.
        In a `profile` or the path of a `path_extrude` it is a spline point when `smooth` is True (the curve goes smoothly through it, in the direction
        its gizmo's rotation gives) and a corner when False; None (the default): what the profile says.  In a `profile` it can also be a ROUND corner, `radius=r`: an exact arc
        of that radius, tangent to both edges, takes the place of the corner (the scale of its gizmo multiplies the radius; a spline point has no radius).  A double click on its
        dot goes from a corner to a spline point to a round corner and back to a corner '''
    if isinstance(x, (tuple, list)) and y == 0.0 and z == 0.0 and len(x) == 3:
        x, y, z = x
    if radius is not None:
        # (said before the point is made: a point that is refused is never half-made)
        value = float(radius(0.0, 0.0, 0.0)) if isinstance(radius, Shape) else float(radius)
        if value < 0:
            raise ValueError('point(..., radius=%g): a radius is not negative -- 0 is a hard corner, a positive number rounds it with an arc of that radius' % value)
        if smooth:
            raise ValueError('point(..., smooth=True, radius=%g): a spline point has no radius, because the curve goes smoothly through it and there is no corner to round -- '
                             'a point is a corner (smooth=False), a spline point (smooth=True) or a round corner (radius=r: a corner with a radius)' % value)
    if influence is not None:
        value = float(influence(0.0, 0.0, 0.0)) if isinstance(influence, Shape) else float(influence)
        if value < 0:
            raise ValueError('point(..., influence=%g): an influence is not negative -- it is how far the point reaches, the radius of the field round it' % value)
    return Point(x, y, z, smooth, radius, influence)


def image_dot(source, xyz, rows, about, shift=(0.0, 0.0, 0.0), frozen=()):
    ''' The dot of a point that is drawn somewhere else than the point is: the IMAGE of `source` under the map that takes a place to `about + rows * (place - about) + shift` (a turn or a
        mirror image, then a move), at `xyz` -- expressions of the numbers of `source`.  It is the point in every way that is seen: it is pressed, dragged, double clicked, and it is
        `source` that all of those change -- the numbers of `source`'s gizmo are what it is dragged by, taken back through the map (the viewport does it: `_symmetry`) --, so that the
        original and everything that is an image of it change together.  `frozen` are the numbers of the map itself (a gizmo's): a drag of the dot never changes them.  The dots of the
        images of a profile or a line in a symmetry, and of a placed copy of one (see handles.gizmo), are these '''
    out = Point(xyz[0], xyz[1], xyz[2], smooth=getattr(source, '_smooth', None), radius=getattr(source, '_radius', None), influence=getattr(source, '_influence', None))
    out._kind = getattr(source, '_kind', 'point')
    out._symmetry_linear = rows
    g = getattr(source, '_gizmo', None)
    if g is not None:
        out._gizmo = g
        out._symmetry = (tuple(v for row in rows for v in row), tuple(about), tuple(shift))
    out._no_handles = True
    out._gizmo_dot_only = True          # (it has no gizmo of its own to show: the original point has)
    out._symmetry_of = source
    if frozen:
        out._symmetry_vars = tuple(frozen)
    return out


def surface(field):
    ''' An open surface from any field: where the field is 0.  (A body's field as a surface is its skin.) '''
    return Surface(field)


def plane(point=(0, 0, 0), normal=(0, 0, 1)):
    ''' A plane through `point`, positive on the side `normal` points to: a surface '''
    from fieldes.stdlib.fields import distance_to_plane
    return Surface(distance_to_plane(point, normal))


def sphere_surface(radius=10.0, center=(0, 0, 0)):
    ''' The surface of a sphere (without its inside): a surface, negative inside '''
    from fieldes.stdlib.fields import distance_to_point
    return Surface(distance_to_point(center, 0.0) - radius)             # (the plain distance: the radius is the sphere's own, not the influence of a centre that is a point)


def cylinder_surface(radius=5.0, axis='z', center=(0, 0, 0)):
    ''' The surface of an infinite cylinder along an axis: a surface, negative inside '''
    from fieldes.stdlib.fields import _radial_distance
    return Surface(_radial_distance(center, axis) - radius)             # (the plain distance: the radius is the cylinder's own, not the influence of a centre that is a point)


def wave_surface(amplitude=2.0, period=10.0, axis='x', height=0.0):
    ''' A wavy sheet z = height + amplitude sin(2 pi s / period) along an axis (s = x or y): a surface, positive above it '''
    from fieldes.stdlib.fields import wave
    return Surface(Shape.Z() - (height + wave(axis, period, amplitude)))


def displayed(shape):
    ''' What FielDes draws for a shape (see the top of this file) '''
    kind = kind_of(shape)
    if kind == 'point':
        return _weld_displayed(shape)
    if kind == 'profile':
        # The same field: the app meshes it as the face where it is negative, in the plane z = 0 -- or the plane the shape says it lies in, `_flat_frame`
        # (origin, u, v: a profile) -- (libfive Mesh::renderFlat), not as a body
        out = _as(shape, shape)
        out._flat2d = True
        return out
    if kind == 'field':
        # A field is not a body: nothing of it is drawn in the viewport.  A displayed field has an icon, which opens the field viewer: it shows
        # the field at every point of a disc
        from fieldes.stdlib.shapes import emptiness
        return emptiness()
    return shape
