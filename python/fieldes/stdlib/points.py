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

    def __init__(self, x=0.0, y=0.0, z=0.0, smooth=None):
        self.xyz = (_coord(x), _coord(y), _coord(z))
        self._kind = 'point'                    # (an instance attribute: it goes with the point through gizmo() and the like)
        self._smooth = None if smooth is None else bool(smooth)      # (what it is in a sketch: a spline point or a corner; None: what the sketch says)
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
        return self


class Surface(Shape):
    ''' An open surface: the zero set of a field, with no body behind it (negative on one side, positive on the other) '''
    _kind = 'surface'

    def __init__(self, field):
        field = Shape.wrap(field)
        super().__init__(lib.libfive_tree_copy(field.ptr))
        self._kind = 'surface'                  # (an instance attribute: it goes with the surface through gizmo() and the like)

    def _display(self):
        # The field itself: the mesher finds where it is zero, and that is the sheet -- one layer of triangles, no thickness
        return _as(self, self)


def point(x=0.0, y=0.0, z=0.0, smooth=None):
    ''' A point at (x, y, z).  Its coordinates are numbers or fields.  It is drawn as a dot (the middle point of a gizmo): click it, and its gizmo shows.
        In a `sketch` or the path of a `path_extrude` it is a spline point when `smooth` is True (the curve goes smoothly through it, in the direction
        its gizmo's rotation gives) and a corner when False; None (the default): what the sketch says.  A double click on its dot toggles it '''
    if isinstance(x, (tuple, list)) and y == 0.0 and z == 0.0 and len(x) == 3:
        x, y, z = x
    return Point(x, y, z, smooth)


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
    return Surface(distance_to_point(center) - radius)


def cylinder_surface(radius=5.0, axis='z', center=(0, 0, 0)):
    ''' The surface of an infinite cylinder along an axis: a surface, negative inside '''
    from fieldes.stdlib.fields import radial_field
    return Surface(radial_field(center, axis) - radius)


def wave_surface(amplitude=2.0, period=10.0, axis='x', height=0.0):
    ''' A wavy sheet z = height + amplitude sin(2 pi s / period) along an axis (s = x or y): a surface, positive above it '''
    from fieldes.stdlib.fields import wave
    return Surface(Shape.Z() - (height + wave(axis, period, amplitude)))


def displayed(shape):
    ''' What FielDes draws for a shape (see the top of this file) '''
    kind = kind_of(shape)
    if kind == 'profile':
        # The same field: the app meshes it as the face where it is negative, in the plane z = 0 -- or the plane the shape says it lies in, `_flat_frame`
        # (origin, u, v: a sketch) -- (libfive Mesh::renderFlat), not as a body
        out = _as(shape, shape)
        out._flat2d = True
        return out
    if kind == 'field':
        # A field is not a body: nothing of it is drawn in the viewport.  A displayed field has an icon, which opens the field viewer: it shows
        # the field at every point of a plane inside the field's bounds
        from fieldes.stdlib.shapes import emptiness
        return emptiness()
    return shape
