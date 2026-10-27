'''
Lattices: periodic TPMS and strut lattices, field-driven thickness, cell
maps (Cartesian, cylindrical, spherical), and filling bodies.

A lattice that repeats is made of a CELL: the one cell it is made of.  Four functions make one --

    cell_periodic(kind)              a standard cell: a strut cell (octet, bcc, kelvin ...), a TPMS
                                     (gyroid, schwarz_p ...) or a planar pattern (hexagon ...)
    cell_custom(region, geometry)    a custom cell of ANY geometry: a box for the extent of the cell and a field for
                                     what is in it -- the cell is their intersection, and repeats on any cell map
    cell_custom_truss(nodes, beams)  a custom strut cell: beams between nodes in the unit cube
    cell_custom_tpms(equation)       a custom triply periodic surface, from its equation

-- and lattice(), lattice_surface_conform(), strut_lattice(), tpms() ... take it as their `cell`, and nothing else: a
name such as 'gyroid' is not a cell, cell_periodic('gyroid') is.  The cell is only WHAT the lattice is made of; how thick
it is, how big, and where it goes (a body, a surface, a cell map) are the operation's.

A foam does not repeat, so it is not made of a cell: it is made around POINTS.  Three steps, each what its name says --

    random_points(body, ...)         points in a body: how far apart they are on average, how random, the nearest and the farthest
                                     two may be.  FielDes draws them, each as the dot of a gizmo
    voronoi_lattice(points, ...)     the edges of the Voronoi cells of the points, as beams: a foam
    delaunay_lattice(points, ...)    the Delaunay edges between the points, as beams: a stochastic truss

-- a lattice made of points fills the body the points were made in (so it takes no body of its own).

The surface of the body cuts a foam off.  random_points(body, surface=True) puts points ON the surface as well, and the foam then has a clean net of beams all over it;
surface_lattice(body, cell_size) is such a net on its own: a random lattice of one layer, that does not repeat, over the surface of any body.

The one-call version:

    from fieldes import *

    part = sphere(30)
    lat = lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=1.0, skin=1.5)

    # graded: thicker walls near the skin, by a ramp or a regression
    t = ramp(depth_below(part), (0, 30), (1.6, 0.5))
    lat = lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=t, skin=1.5)

    # by relative density instead of thickness (calibrated automatically;
    # the density may itself be a field)
    lat = lattice(part, cell_periodic('octet'), cell_size=10, density=0.2)

    # a foam: points in the body, and the Voronoi cells around them
    points = random_points(part, spacing=8, randomness=0.7)
    foam = voronoi_lattice(points, thickness=1.0)

    # cells that follow a sphere / cylinder
    lat = lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=1.0,
                  cell_map=spherical(cells_around=16))

Building blocks (infinite lattices -- fields; fill() or lattice() trims them):
    tpms(cell, cell_size, thickness=..., style='sheet' | 'network', ...)
    strut_lattice(cell, cell_size, radius=... | thickness=..., node_radius=None, blend=0, ...)
    planar_lattice(cell, cell_size, wall, axis='z')      honeycombs, grids
    fill(body, lattice, skin=0, region='volume' | 'shell', depth=...)
    relative_density(lattice, cell_size)                 volume fraction
    lattice_parameter_for_density(cell, cell_size, density)

TPMS kinds: gyroid, schwarz_p, diamond (Schwarz D), neovius, lidinoid,
    split_p, iwp, frd, fischer_koch_s.  style='sheet' is a wall of the given
    thickness on both sides of the minimal surface ("walled TPMS");
    style='network' is the solid on one side of it, grown by `offset`
    ("skeletal TPMS").
Strut cells: cubic, bcc, bccz, fcc, fccz, octet, octahedron, kelvin,
    diamond, cross, tesseract, cuboctahedron.
Planar (2.5D) patterns: hexagon (honeycomb), triangle, square, kagome.
(All of them are names for cell_periodic(); the kinds are listed there.)

Thickness, radius, offset, density, node radius -- any of them can be a
field (a Shape), e.g. a regression of test data applied to a distance field.

Units are mm like everything else; cell_size may be (sx, sy, sz).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import collections
import itertools
import math
import numbers
import random
import sys

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib.content_cache import content_cached
from fieldes.stdlib.fieldargs import is_field, present, positive, number_or_field, returns_lattice

__all__ = ['cell_periodic', 'cell_custom', 'cell_custom_truss', 'cell_custom_tpms', 'LatticeCell',
           'random_points', 'point_cloud', 'PointSet', 'voronoi_lattice', 'delaunay_lattice', 'surface_lattice',
           'lattice', 'tpms', 'strut_lattice', 'planar_lattice', 'fill',
           'graph_lattice', 'voronoi_graph',
           'surface_graph', 'points_graph', 'LatticeGraph',
           'cartesian', 'cylindrical', 'spherical', 'CellMap',
           'relative_density', 'lattice_parameter_for_density',
           'TPMS_KINDS', 'STRUT_CELLS', 'PLANAR_KINDS']

X, Y, Z = Shape.X, Shape.Y, Shape.Z


################################################################################
# Forward-mode derivatives over Shapes (so TPMS fields can be divided by
# their gradient length: an approximately true distance, which makes
# `thickness` a real wall thickness in mm)

def _num(v):
    return isinstance(v, numbers.Number)


def _mul(a, b):
    if _num(a):
        if a == 0:
            return 0.0
        if a == 1:
            return b
    if _num(b):
        if b == 0:
            return 0.0
        if b == 1:
            return a
    return a * b


def _add(a, b):
    if _num(a) and a == 0:
        return b
    if _num(b) and b == 0:
        return a
    return a + b


def _neg(a):
    return -a


def _f(name, v):
    if _num(v):
        return getattr(math, name)(v)
    return getattr(v, name)()


class _D:
    ''' value + gradient (3 components; numbers where known constant) '''
    __slots__ = ('v', 'g')

    def __init__(self, v, g=(0.0, 0.0, 0.0)):
        self.v = v
        self.g = tuple(g)

    @staticmethod
    def of(a):
        return a if isinstance(a, _D) else _D(a)

    def __add__(self, o):
        o = _D.of(o)
        return _D(_add(self.v, o.v), [_add(a, b) for a, b in zip(self.g, o.g)])
    __radd__ = __add__

    def __sub__(self, o):
        o = _D.of(o)
        return _D(_add(self.v, _mul(-1.0, o.v)), [_add(a, _mul(-1.0, b)) for a, b in zip(self.g, o.g)])

    def __rsub__(self, o):
        return _D.of(o) - self

    def __neg__(self):
        return _D(_mul(-1.0, self.v), [_mul(-1.0, a) for a in self.g])

    def __mul__(self, o):
        o = _D.of(o)
        return _D(_mul(self.v, o.v),
                  [_add(_mul(a, o.v), _mul(b, self.v)) for a, b in zip(self.g, o.g)])
    __rmul__ = __mul__

    def __truediv__(self, o):
        o = _D.of(o)
        if _num(o.v) and all(_num(c) and c == 0 for c in o.g):
            return self * (1.0 / o.v)
        q = self.v / o.v
        return _D(q, [_mul(_add(a, _mul(_mul(-1.0, q), b)), 1.0 / o.v if _num(o.v) else 1 / o.v)
                      for a, b in zip(self.g, o.g)])

    def sin(self):
        c = _f('cos', self.v)
        return _D(_f('sin', self.v), [_mul(a, c) for a in self.g])

    def cos(self):
        s = _f('sin', self.v)
        return _D(_f('cos', self.v), [_mul(a, _mul(-1.0, s)) for a in self.g])

    def sqrt(self):
        r = _f('sqrt', self.v)
        k = 0.5 / r if _num(r) else 0.5 / r.max(1e-12)
        return _D(r, [_mul(a, k) for a in self.g])

    def square(self):
        return self * self

    def mod(self, m):
        return _D(self.v % m, self.g)

    def atan2(self, x):
        ''' atan2(self, x) '''
        x = _D.of(x)
        y = self
        v = y.v.atan2(x.v) if not _num(y.v) else (
            Shape.wrap(y.v).atan2(x.v) if not _num(x.v) else math.atan2(y.v, x.v))
        d = _add(_mul(x.v, x.v), _mul(y.v, y.v))
        inv = 1.0 / d if _num(d) else 1 / d.max(1e-12)
        return _D(v, [_mul(_add(_mul(x.v, gy), _mul(_mul(-1.0, y.v), gx)), inv)
                      for gx, gy in zip(x.g, y.g)])

    def grad_length(self):
        s = 0.0
        for a in self.g:
            s = _add(s, _mul(a, a))
        return s


def _xyz():
    return (_D(X(), (1.0, 0.0, 0.0)), _D(Y(), (0.0, 1.0, 0.0)), _D(Z(), (0.0, 0.0, 1.0)))


################################################################################
# Cell maps: where the lattice's cells go

def _rotation(rot):
    ''' Rotation matrix from (rx, ry, rz) in degrees (applied x, then y, then z) '''
    rx, ry, rz = (math.radians(float(a)) for a in rot)
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    Rx = [[1, 0, 0], [0, cx, -sx], [0, sx, cx]]
    Ry = [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]
    Rz = [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]

    def mm(A, B):
        return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mm(Rz, mm(Ry, Rx))


class LatticeCell:
    ''' What a lattice is made of (see the module): made by cell_periodic(), cell_custom(),
        cell_custom_truss() or cell_custom_tpms(), taken by every lattice operation as its `cell`.
        .family      'strut' (beams between nodes), 'tpms' (a periodic surface), 'planar' (a 2.5D pattern) or 'shape'
                     (any geometry in a box, see cell_custom)
        .periodic    whether the cell repeats on a grid (so can follow a cell map or a surface) '''
    family = ''
    periodic = True
    name = ''

    def __repr__(self):
        return '{}({!r})'.format(type(self).__name__, self.name)


class CellMap:
    ''' Maps space to lattice coordinates (u, v, w), in mm, in which the
        lattice repeats every cell size '''
    kind = 'cartesian'

    def __init__(self, origin=(0, 0, 0), rotation=None):
        self.origin = tuple(float(c) for c in origin)
        self.R = _rotation(rotation) if rotation else None

    def _local(self, p):
        x, y, z = (p[i] - self.origin[i] for i in range(3))
        if self.R is None:
            return x, y, z
        R = self.R
        # u = R^T (p - o): the lattice turned by R
        return (x * R[0][0] + y * R[1][0] + z * R[2][0],
                x * R[0][1] + y * R[1][1] + z * R[2][1],
                x * R[0][2] + y * R[1][2] + z * R[2][2])

    def map(self, p, cell):
        ''' The point p as lattice coordinates (u, v, w) in mm, in which the lattice repeats every `cell` '''
        return self._local(p)

    def describe(self, cell):
        ''' A short description of the mapping, for the output pane '''
        return 'Cartesian cells'


def cartesian(origin=(0, 0, 0), rotation=None):
    ''' Straight cells, optionally shifted (origin) and turned (rotation =
        (rx, ry, rz) degrees) '''
    return CellMap(origin, rotation)


class _Cylindrical(CellMap):
    kind = 'cylindrical'

    def __init__(self, origin, axis, cells_around, radius, rotation=None):
        CellMap.__init__(self, origin, rotation)
        self.axis = axis
        self.cells_around = cells_around
        self.radius = radius

    def count(self, cell):
        if self.cells_around:
            return int(self.cells_around)
        if self.radius:
            return max(3, int(round(2 * math.pi * float(self.radius) / cell[1])))
        raise ValueError('cylindrical(): give cells_around= or radius= (where the cells '
                         'should be cell_size wide)')

    def map(self, p, cell):
        x, y, z = self._local(p)
        a, b, w = {'z': (x, y, z), 'x': (y, z, x), 'y': (z, x, y)}[self.axis]
        r = (a * a + b * b).sqrt()
        theta = b.atan2(a)
        n = self.count(cell)
        # v runs n cells around the axis: theta * n * cell / (2 pi)
        return r, theta * (n * cell[1] / (2 * math.pi)), w

    def describe(self, cell):
        return 'cylindrical cells, {} around the {} axis'.format(self.count(cell), self.axis)


def cylindrical(origin=(0, 0, 0), axis='z', cells_around=None, radius=None, rotation=None):
    ''' Cells that wrap around an axis: radial, around, and along the axis.
        Give the number of cells around (cells_around=) or the radius at
        which cells are cell_size wide (radius=). '''
    return _Cylindrical(origin, axis, cells_around, radius, rotation)


class _Spherical(CellMap):
    kind = 'spherical'

    def __init__(self, origin, cells_around, radius, rotation=None):
        CellMap.__init__(self, origin, rotation)
        self.cells_around = cells_around
        self.radius = radius

    def count(self, cell):
        if self.cells_around:
            return int(self.cells_around)
        if self.radius:
            return max(4, int(round(2 * math.pi * float(self.radius) / cell[1])))
        raise ValueError('spherical(): give cells_around= or radius= (where the cells '
                         'should be cell_size wide)')

    def map(self, p, cell):
        x, y, z = self._local(p)
        rho = (x * x + y * y + z * z).sqrt()
        theta = y.atan2(x)                                   # around z
        phi = (x * x + y * y).sqrt().atan2(z)                # from +z
        n = self.count(cell)
        m = max(2, int(round(n / 2.0)))
        return (rho, theta * (n * cell[1] / (2 * math.pi)),
                phi * (m * cell[2] / math.pi))

    def describe(self, cell):
        n = self.count(cell)
        return 'spherical cells, {} around, {} pole to pole'.format(n, max(2, int(round(n / 2.0))))


def spherical(origin=(0, 0, 0), cells_around=None, radius=None, rotation=None):
    ''' Cells in shells around a point: radial, around (longitude) and pole
        to pole (latitude).  Give cells_around= or radius= (where the cells
        should be cell_size wide).  Cells shrink towards the poles. '''
    return _Spherical(origin, cells_around, radius, rotation)


def _cell3(cell_size):
    if isinstance(cell_size, numbers.Number):
        c = float(cell_size)
        if c <= 0:
            raise ValueError('cell_size must be positive')
        return (c, c, c)
    c = tuple(float(v) for v in cell_size)
    if len(c) != 3 or min(c) <= 0:
        raise ValueError('cell_size is a number or (sx, sy, sz), all positive')
    return c


################################################################################
# TPMS

def _s2(a):
    return (a * 2.0).sin()


def _c2(a):
    return (a * 2.0).cos()


def _gyroid(a, b, c):
    return a.sin() * b.cos() + b.sin() * c.cos() + c.sin() * a.cos()


def _schwarz_p(a, b, c):
    return a.cos() + b.cos() + c.cos()


def _diamond(a, b, c):
    sa, sb, sc, ca, cb, cc = a.sin(), b.sin(), c.sin(), a.cos(), b.cos(), c.cos()
    return sa * sb * sc + sa * cb * cc + ca * sb * cc + ca * cb * sc


def _neovius(a, b, c):
    ca, cb, cc = a.cos(), b.cos(), c.cos()
    return 3.0 * (ca + cb + cc) + 4.0 * ca * cb * cc


def _lidinoid(a, b, c):
    return (0.5 * (_s2(a) * b.cos() * c.sin() + _s2(b) * c.cos() * a.sin() + _s2(c) * a.cos() * b.sin())
            - 0.5 * (_c2(a) * _c2(b) + _c2(b) * _c2(c) + _c2(c) * _c2(a)) + 0.15)


def _split_p(a, b, c):
    return (1.1 * (_s2(a) * c.sin() * b.cos() + _s2(b) * a.sin() * c.cos() + _s2(c) * b.sin() * a.cos())
            - 0.2 * (_c2(a) * _c2(b) + _c2(b) * _c2(c) + _c2(c) * _c2(a))
            - 0.4 * (_c2(a) + _c2(b) + _c2(c)))


def _iwp(a, b, c):
    ca, cb, cc = a.cos(), b.cos(), c.cos()
    return 2.0 * (ca * cb + cb * cc + cc * ca) - (_c2(a) + _c2(b) + _c2(c))


def _frd(a, b, c):
    return 4.0 * a.cos() * b.cos() * c.cos() - (_c2(a) * _c2(b) + _c2(b) * _c2(c) + _c2(c) * _c2(a))


def _fischer_koch_s(a, b, c):
    return _c2(a) * b.sin() * c.cos() + a.cos() * _c2(b) * c.sin() + a.sin() * b.cos() * _c2(c)


TPMS_KINDS = {
    'gyroid': _gyroid, 'schwarz_p': _schwarz_p, 'schwarz': _schwarz_p,
    'diamond': _diamond, 'schwarz_d': _diamond, 'neovius': _neovius,
    'lidinoid': _lidinoid, 'split_p': _split_p, 'iwp': _iwp, 'frd': _frd,
    'fischer_koch_s': _fischer_koch_s,
}


def _tpms_distance(kind, cell, cell_map, fast=False):
    """ Approximate signed distance (mm) to the TPMS zero surface: one Newton
        step (f / |grad f|), refined by a second step from the projected
        point unless fast=True (walls within ~1 % of the asked thickness
        instead of ~5 %, at about twice the rendering cost) """
    if isinstance(kind, TPMSEquation):
        F = kind.func
    else:
        key = kind.lower().replace('-', '_').replace(' ', '_')
        if key not in TPMS_KINDS:
            raise ValueError('unknown TPMS {!r}; kinds: {}'.format(kind, ', '.join(sorted(TPMS_KINDS))))
        F = TPMS_KINDS[key]

    def value(q):
        u = cell_map.map(q, cell)
        return F(*[u[i] * (2 * math.pi / cell[i]) for i in range(3)])
    k = 2 * math.pi / min(cell)
    eps2 = (0.15 * k) ** 2          # keeps f / |grad f| finite where grad f vanishes
    p = _xyz()
    f = value(p)
    g2 = _add(f.grad_length(), eps2)
    d1 = f.v / g2.sqrt()
    if fast:
        return d1
    # second step, from p - f grad f / |grad f|^2; its correction is limited
    # to a fraction of the first so a step onto another sheet (far from the
    # surface) can't make the field jump
    p1 = [_D(Shape.wrap(p[i].v) - f.v * f.g[i] / g2,
             tuple(1.0 if j == i else 0.0 for j in range(3))) for i in range(3)]
    f1 = value(p1)
    corr = f1.v / _add(f1.grad_length(), eps2).sqrt()
    lim = 0.3 * abs(d1)
    return d1 + corr.max(-lim).min(lim)


def tpms(cell, cell_size=10.0, thickness=1.0, style='sheet', offset=0.0,
         cell_map=None, invert=False, fast=False):
    ''' An infinite TPMS lattice (a field; trim it with fill() or use
        lattice()).
        cell: a TPMS cell -- cell_periodic('gyroid' | 'schwarz_p' | 'diamond' | 'neovius' | 'lidinoid' |
            'split_p' | 'iwp' | 'frd' | 'fischer_koch_s'), or your own: cell_custom_tpms(f)
        style='sheet': walls of `thickness` mm centred on the surface
        style='network': the solid on one side of the surface, grown by
            `offset` mm (0 = half the volume for gyroid / diamond / P);
            invert=True takes the other side
        style='surface': the signed distance to the surface itself
        thickness / offset may be fields.
        fast=True: a quicker, slightly less exact distance (walls ~5 % thin) '''
    kind = _need_cell(cell, 'tpms', 'tpms')
    cell = _cell3(cell_size)
    d = _tpms_distance(_plain(kind), cell, cell_map or CellMap(), fast)
    if style == 'sheet':
        return abs(d) - Shape.wrap(thickness) / 2
    if style == 'network':
        return (-d if invert else d) - offset
    if style == 'surface':
        return d
    raise ValueError("tpms: style is 'sheet', 'network' or 'surface'")


################################################################################
# Strut lattices

def _corners():
    return [(i, j, k) for i in (0, 1) for j in (0, 1) for k in (0, 1)]


def _pairs_at(points, dist, tol=1e-6):
    out = []
    for a, b in itertools.combinations(points, 2):
        if abs(math.dist(a, b) - dist) < tol:
            out.append((a, b))
    return out


def _cell_cubic():
    return _pairs_at(_corners(), 1.0)


def _cell_bcc():
    c = (0.5, 0.5, 0.5)
    return [(c, p) for p in _corners()]


def _verticals():
    return [((i, j, 0), (i, j, 1)) for i in (0, 1) for j in (0, 1)]


def _cell_fcc():
    beams = []
    for axis in range(3):
        for side in (0, 1):
            pts = [p for p in _corners() if p[axis] == side]
            beams += _pairs_at(pts, math.sqrt(2))
    return beams


def _face_centres():
    out = []
    for axis in range(3):
        for side in (0.0, 1.0):
            p = [0.5, 0.5, 0.5]
            p[axis] = side
            out.append(tuple(p))
    return out


def _cell_octahedron():
    return _pairs_at(_face_centres(), math.sqrt(0.5))


def _cell_octet():
    return _cell_fcc() + _cell_octahedron()


def _cell_kelvin():
    verts = set()
    for perm in itertools.permutations((0.5, 0.25, 0.0)):
        for sx in (-1, 1):
            for sy in (-1, 1):
                for sz in (-1, 1):
                    verts.add(tuple(0.5 + s * v for s, v in zip((sx, sy, sz), perm)))
    return _pairs_at(sorted(verts), math.sqrt(2) / 4)


def _cell_diamond():
    A = [(0, 0, 0), (0, 0.5, 0.5), (0.5, 0, 0.5), (0.5, 0.5, 0)]
    beams = []
    for a in A:
        b = tuple(c + 0.25 for c in a)
        for s in ((1, 1, 1), (1, -1, -1), (-1, 1, -1), (-1, -1, 1)):
            beams.append((b, tuple(b[i] - 0.25 * s[i] for i in range(3))))
    return beams


def _cell_cross():
    c = (0.5, 0.5, 0.5)
    return [(c, f) for f in _face_centres()]


def _cell_tesseract():
    inner = [tuple(0.25 + 0.5 * v for v in p) for p in _corners()]
    return (_cell_cubic() + _pairs_at(inner, 0.5) +
            [(p, tuple(0.25 + 0.5 * v for v in p)) for p in _corners()])


def _cell_cuboctahedron():
    mids = set()
    for a, b in _cell_cubic():
        mids.add(tuple((a[i] + b[i]) / 2 for i in range(3)))
    return _pairs_at(sorted(mids), math.sqrt(0.5))


STRUT_CELLS = {
    'cubic': _cell_cubic, 'simple_cubic': _cell_cubic,
    'bcc': _cell_bcc, 'bccz': lambda: _cell_bcc() + _verticals(),
    'fcc': _cell_fcc, 'fccz': lambda: _cell_fcc() + _verticals(),
    'octet': _cell_octet, 'octahedron': _cell_octahedron,
    'kelvin': _cell_kelvin, 'truncated_octahedron': _cell_kelvin,
    'diamond': _cell_diamond, 'cross': _cell_cross,
    'tesseract': _cell_tesseract, 'cuboctahedron': _cell_cuboctahedron,
}


def _canon(beam):
    a, b = (tuple(round(c, 9) for c in p) for p in beam)
    return (a, b) if a <= b else (b, a)


def _clip(a, b, lo, hi):
    ''' Clips segment a-b to the box [lo, hi] (Liang-Barsky); None if outside '''
    t0, t1 = 0.0, 1.0
    for i in range(3):
        d = b[i] - a[i]
        for p, q in ((-d, a[i] - lo[i]), (d, hi[i] - a[i])):
            if p == 0:
                if q < 0:
                    return None
            else:
                t = q / p
                if p < 0:
                    t0 = max(t0, t)
                else:
                    t1 = min(t1, t)
        if t0 > t1 + 1e-12:
            return None
    pa = tuple(a[i] + t0 * (b[i] - a[i]) for i in range(3))
    pb = tuple(a[i] + t1 * (b[i] - a[i]) for i in range(3))
    if math.dist(pa, pb) < 1e-9:
        return None
    return (pa, pb)


def unit_cell_beams(cell):
    ''' The beams of a strut cell (in unit-cube coordinates), made
        periodic: every beam of the infinite lattice that passes near the
        cell, clipped to it (with a margin for cells that aren't
        mirror-symmetric).  Returns (beams, margin). '''
    cell = _need_cell(cell, 'unit_cell_beams', 'strut')
    radius_of = cell.radii() if isinstance(cell, UnitCell) else {}
    motif = cell() if isinstance(cell, UnitCell) else STRUT_CELLS[cell.key]()
    # the infinite lattice around the cell, clipped to the cell (each
    # clipped piece remembers its beam's own radius, if it has one)
    radii = {}

    def around(margin):
        out = set()
        lo, hi = (-margin,) * 3, (1 + margin,) * 3
        for off in itertools.product((-1, 0, 1), repeat=3):
            for a, b in motif:
                a2 = tuple(a[i] + off[i] for i in range(3))
                b2 = tuple(b[i] + off[i] for i in range(3))
                c = _clip(a2, b2, lo, hi)
                if c:
                    key = _canon(c)
                    out.add(key)
                    r = radius_of.get(_canon((a, b)))
                    if r is not None:
                        radii[key] = r
        return out
    own = around(0.0)
    # mirror-symmetric about the cell faces: the cell's own beams suffice
    symmetric = True
    for axis in range(3):
        refl = set()
        for a, b in own:
            ra = tuple(1 - a[i] if i == axis else a[i] for i in range(3))
            rb = tuple(1 - b[i] if i == axis else b[i] for i in range(3))
            refl.add(_canon((ra, rb)))
        if refl != own:
            symmetric = False
            break
    unit_cell_beams.radii = radii      # (strut_lattice: per-beam radii)
    if symmetric:
        return sorted(own), 0.0
    return sorted(around(0.5)), 0.5


def _capsules(beams, q, cell):
    ''' Distances (mm) from local cell coordinates q (mm) to each beam '''
    out = []
    for a, b in beams:
        A = tuple(a[i] * cell[i] for i in range(3))
        B = tuple(b[i] * cell[i] for i in range(3))
        ab = tuple(B[i] - A[i] for i in range(3))
        L2 = sum(c * c for c in ab)
        pa = [q[i] - A[i] for i in range(3)]
        h = ((pa[0] * ab[0] + pa[1] * ab[1] + pa[2] * ab[2]) * (1.0 / L2)).max(0).min(1)
        out.append(((pa[0] - h * ab[0]).square() + (pa[1] - h * ab[1]).square() +
                    (pa[2] - h * ab[2]).square()).sqrt())
    return out


def _balanced(items, op):
    items = list(items)
    while len(items) > 1:
        items = [op(items[i], items[i + 1]) if i + 1 < len(items) else items[i]
                 for i in range(0, len(items), 2)]
    return items[0]


def _smin(a, b, k):
    if is_field(k):
        k = k.max(1e-9)                 # (a blend that is 0 somewhere is sharp there)
    h = (k - abs(a - b)).max(0) / k
    return a.min(b) - h * h * k / 4


def _beam_radius(who, thickness, radius, default):
    ''' The radius of round beams from what the caller gave: `thickness` (their diameter) or `radius` (half of it), numbers or
        fields; both is a mistake, none gives `default` '''
    if thickness is not None and radius is not None:
        raise ValueError('{}: give the beams\' size as thickness (their diameter) or as radius (half of it), not both'.format(who))
    if thickness is not None:
        return Shape.wrap(thickness) / 2 if isinstance(thickness, Shape) else float(thickness) / 2.0
    return radius if radius is not None else default


def strut_lattice(cell, cell_size=10.0, radius=None, node_radius=None, blend=0.0,
                  cell_map=None, thickness=None):
    ''' An infinite strut (beam) lattice: round beams of `thickness` mm (their diameter;
        or `radius` mm, which is half of it: give one of the two, default radius 0.8)
        along the edges of a unit cell, repeated every cell_size.
        cell: a strut cell -- cell_periodic('cubic' | 'bcc' | 'bccz' | 'fcc' | 'fccz' | 'octet' | 'octahedron' |
            'kelvin' | 'diamond_struts' | 'cross' | 'tesseract' | 'cuboctahedron'), or your own:
            cell_custom_truss(nodes, beams) (beams may have their own radius)
        node_radius: spheres at the joints (defaults to none)
        blend: rounds the joints with a smooth blend of this radius
        thickness / radius / node_radius may be fields. '''
    cell = _need_cell(cell, 'strut_lattice', 'strut')
    radius = _beam_radius('strut_lattice', thickness, radius, 0.8)
    c = _cell3(cell_size)
    beams, _ = unit_cell_beams(cell)
    own = dict(getattr(unit_cell_beams, 'radii', {}))
    m = cell_map or CellMap()
    u = m.map((X(), Y(), Z()), c)
    q = [u[i] % c[i] for i in range(3)]
    d = _capsules(beams, q, c)
    if own:
        # beams with their own radius (a UnitCell): each capsule its radius
        d = [di - (own[b] if b in own else radius) for di, b in zip(d, beams)]
    if present(blend) and positive(blend):
        k = number_or_field(blend, 'the blend')
        dist = _balanced(d, lambda a, b: _smin(a, b, k))
    else:
        dist = _balanced(d, lambda a, b: a.min(b))
    out = dist if own else dist - radius
    if node_radius is not None:
        nodes = sorted({p for b in beams for p in b
                        if all(-1e-9 <= v <= 1 + 1e-9 for v in p)})
        spheres = [((q[0] - p[0] * c[0]).square() + (q[1] - p[1] * c[1]).square() +
                    (q[2] - p[2] * c[2]).square()).sqrt() for p in nodes]
        out = out.min(_balanced(spheres, lambda a, b: a.min(b)) - node_radius)
    return out


################################################################################
# Planar (2.5D) patterns: walls extruded along an axis

def _planar_motif(kind):
    ''' Segments in a rectangular periodic cell (width 1, height aspect) '''
    k = kind.lower()
    if k in ('hexagon', 'honeycomb', 'hex'):
        # flat-to-flat width 1 -> edge a = 1/sqrt(3); cell 3a x sqrt(3)a... in
        # units of the width: the rectangle is (sqrt(3), 1)
        a = 1 / math.sqrt(3)
        w, h = 3 * a, 1.0
        segs = []
        for cx, cy in ((0, 0), (1.5 * a, 0.5), (3 * a, 0), (0, 1), (3 * a, 1), (1.5 * a, -0.5),
                       (1.5 * a, 1.5)):
            ang = [math.radians(60 * i) for i in range(7)]
            pts = [(cx + a * math.cos(t), cy + a * math.sin(t)) for t in ang]
            segs += list(zip(pts[:-1], pts[1:]))
        return segs, (w, h)
    if k == 'triangle':
        h = math.sqrt(3) / 2
        segs = [((0, 0), (1, 0)), ((0, h), (1, h)), ((0, 0), (0.5, h)), ((0.5, h), (1, 0)),
                ((0, 2 * h), (0.5, h)), ((0.5, h), (1, 2 * h)), ((0, 2 * h), (1, 2 * h))]
        return segs, (1.0, 2 * h)
    if k == 'square':
        return [((0, 0), (1, 0)), ((0, 0), (0, 1)), ((1, 0), (1, 1)), ((0, 1), (1, 1))], (1.0, 1.0)
    if k == 'kagome':
        h = math.sqrt(3) / 2
        segs = [((0, 0), (1, 0)), ((0, 2 * h), (1, 2 * h)),
                ((0, 0), (1, 2 * h)), ((1, 0), (0, 2 * h))]
        return segs, (1.0, 2 * h)
    raise ValueError('unknown planar pattern {!r}; kinds: hexagon, triangle, square, kagome'.format(kind))


PLANAR_KINDS = ('hexagon', 'triangle', 'square', 'kagome')


def _clip2(a, b, w, h):
    c = _clip((a[0], a[1], 0.0), (b[0], b[1], 0.0), (0.0, 0.0, -1.0), (w, h, 1.0))
    return None if c is None else ((c[0][0], c[0][1]), (c[1][0], c[1][1]))


def planar_lattice(cell, cell_size=10.0, wall=0.8, axis='z', cell_map=None):
    ''' A 2.5D pattern of walls, extruded along an axis: a honeycomb
        (cell_periodic('hexagon'); cell_size = flat-to-flat width), or a
        triangle, square or kagome grid.  wall is the wall thickness (mm,
        may be a field). '''
    cell = _need_cell(cell, 'planar_lattice', 'planar')
    segs, (w, h) = _planar_motif(_plain(cell))
    s = float(cell_size)
    W, H = w * s, h * s
    m = cell_map or CellMap()
    u = m.map((X(), Y(), Z()), (W, H, s))
    a, b = {'z': (u[0], u[1]), 'x': (u[1], u[2]), 'y': (u[2], u[0])}[axis]
    qa, qb = a % W, b % H
    clipped = set()
    for p, q in segs:
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                c = _clip2((p[0] + ox * w, p[1] + oy * h), (q[0] + ox * w, q[1] + oy * h), w, h)
                if c:
                    c = tuple(tuple(round(v, 9) for v in pt) for pt in c)
                    clipped.add(min(c, c[::-1]))
    d = []
    for (x0, y0), (x1, y1) in sorted(clipped):
        A = (x0 * s, y0 * s)
        B = (x1 * s, y1 * s)
        ab = (B[0] - A[0], B[1] - A[1])
        L2 = ab[0] ** 2 + ab[1] ** 2
        pa = (qa - A[0], qb - A[1])
        t = ((pa[0] * ab[0] + pa[1] * ab[1]) * (1.0 / L2)).max(0).min(1)
        d.append(((pa[0] - t * ab[0]).square() + (pa[1] - t * ab[1]).square()).sqrt())
    return _balanced(d, lambda x, y: x.min(y)) - Shape.wrap(wall) / 2


################################################################################
# Relative density and calibration

def _stats(shape, lo, hi, n):
    f = getattr(lib, 'libfive_tree_grid_stats', None)
    if f is not None:
        out = (ctypes.c_double * 9)()
        ok = f(shape.ptr, (ctypes.c_float * 3)(*lo), (ctypes.c_float * 3)(*hi), n, n, n, out)
        if ok:
            return out[0] / float(n ** 3)
    from fieldes.stdlib.fields import sample_grid
    _, vals = sample_grid(shape, lo, hi, n)
    return sum(1 for v in vals if v < 0) / float(len(vals))


def relative_density(lattice_field, cell_size, samples=40, origin=(0, 0, 0)):
    ''' Volume fraction of an (untrimmed, Cartesian) lattice: the share of
        one unit cell it fills, 0..1 '''
    c = _cell3(cell_size)
    n = int(samples)
    # one period, sampled on a grid shifted by irrational fractions of a
    # step so rows of samples don't line up with beams or walls (which
    # would make the density jump as the member size changes)
    shift = (0.2718281, 0.3183099, 0.1414213)
    lo = tuple(float(origin[i]) + shift[i] * c[i] / n for i in range(3))
    hi = tuple(lo[i] + c[i] for i in range(3))
    return _stats(lattice_field, lo, hi, n)


def _builder(c, cell, style):
    if c.family == 'tpms':
        if style == 'network':
            return (lambda p: tpms(c, cell, style='network', offset=p)), (-0.45 * min(cell), 0.45 * min(cell))
        return (lambda p: tpms(c, cell, thickness=p, style='sheet')), (0.0, 0.9 * min(cell))
    if c.family == 'planar':
        return (lambda p: planar_lattice(c, cell[0], wall=p)), (0.0, 0.9 * cell[0])
    if c.family != 'strut':
        raise ValueError('a density cannot be calibrated for a {} cell'.format(c.family))
    return (lambda p: strut_lattice(c, cell, radius=p)), (0.0, 0.5 * min(cell))


def lattice_parameter_for_density(cell, cell_size, density, style='sheet', samples=40):
    ''' The thickness (sheet TPMS), offset (network TPMS), wall (planar) or
        radius (struts) that gives a lattice of this cell the requested relative density
        (0..1), found by bisection on a sampled unit cell '''
    cellobj = _need_cell(cell, 'lattice_parameter_for_density')
    cell = _cell3(cell_size)
    build, (lo, hi) = _builder(cellobj, cell, style)
    target = float(density)
    if not 0 < target < 1:
        raise ValueError('density is a volume fraction between 0 and 1')
    f_lo = relative_density(build(lo), cell, samples)
    f_hi = relative_density(build(hi), cell, samples)
    if not f_lo <= target <= f_hi:
        raise ValueError('density {} is out of reach for this lattice ({:.3f}..{:.3f})'.format(
            target, f_lo, f_hi))
    for _ in range(30):
        mid = (lo + hi) / 2
        if relative_density(build(mid), cell, samples) < target:
            lo = mid
        else:
            hi = mid
        if hi - lo < 1e-4 * min(cell):
            break
    return (lo + hi) / 2


def _density_map(c, cell, style, samples=32, points=10):
    ''' Monotone map density -> parameter, for density fields '''
    from fieldes.stdlib.regression import fit
    build, (lo, hi) = _builder(c, cell, style)
    table = []
    for i in range(points + 1):
        p = lo + (hi - lo) * i / points
        table.append((relative_density(build(p), cell, samples), p))
    # keep it strictly increasing in density
    clean = []
    for dens, p in table:
        if not clean or dens > clean[-1][0] + 1e-6:
            clean.append((dens, p))
    return fit(clean, model='pchip')


################################################################################
# Filling bodies

def fill(body, lattice_field, skin=0.0, region='volume', depth=None, blend=0.0):
    ''' Trims a lattice to a body.
        skin: a solid skin of this thickness (mm) on the body's surface
        region='volume' fills the whole body; region='shell' only the
            outer `depth` mm (a conformal lattice layer under the skin)
        blend: rounds the joints between lattice and skin '''
    body = Shape.wrap(body)
    lat = Shape.wrap(lattice_field)
    zone = body
    if region == 'shell':
        if depth is None:
            raise ValueError("fill(region='shell') needs depth=")
        zone = body.max(-(body + depth))
    elif region != 'volume':
        raise ValueError("fill: region is 'volume' or 'shell'")
    out = lat.max(zone)
    if present(skin) and positive(skin):
        shell = body.max(-(body + skin))
        if present(blend):
            k = number_or_field(blend, 'the blend')
            if is_field(k):
                k = k.max(1e-9)
            h = (k - abs(out - shell)).max(0) / k
            out = out.min(shell) - h * h * k / 4
            out = out.max(body)           # (the blend must not grow the body)
        else:
            out = out.min(shell)
    b = getattr(body, '_bounds', None)
    if b:
        out._bounds = b
    return out


def lattice(body, cell=None, cell_size=None, thickness=None, radius=None, density=None,
            style='sheet', offset=None, skin=0.0, region='volume', depth=None, cell_map=None,
            node_radius=None, blend=0.0, skin_blend=0.0, wall=None, axis='z'):
    ''' A body filled with a lattice, in one call.

        cell: what it is made of -- cell_periodic(kind) (a TPMS: gyroid, schwarz_p, diamond, neovius, lidinoid, split_p,
            iwp, frd, fischer_koch_s; a strut cell: cubic, bcc, bccz, fcc, fccz, octet, octahedron, kelvin,
            diamond_struts, cross, tesseract, cuboctahedron; a planar pattern: hexagon, triangle, square, kagome),
            cell_custom(region, geometry), cell_custom_truss(nodes, beams) or cell_custom_tpms(equation).
            Default: cell_periodic('gyroid').  (A foam is not a cell: voronoi_lattice(random_points(body)))
        cell_size: mm, or (sx, sy, sz), or a FIELD (cells bigger here and smaller there).  (Default 10 mm; for a
            cell_custom(region, geometry) cell the size of its region, so that it comes out as you modelled it -- a
            larger or smaller size scales the cell.)  A cell size field is blended from lattices of cells a factor 2
            apart (up to a factor of 16 in all): the cells are exact where the field is one of those sizes and a blend of
            the two next to it between them
        thickness: the member size of every cell -- the wall of a sheet TPMS, the diameter of the beams of a strut
            cell (radius= is the same thing for beams, half of it); offset (network TPMS), wall
            (planar).  Numbers or fields
        density: instead of the member size, a relative density 0..1 (a
            number or a field); calibrated automatically
        style: 'sheet' or 'network' (TPMS)
        skin: solid skin thickness on the body's surface (0 = none)
        region: 'volume' or 'shell' (only the outer `depth` mm)
        cell_map: cartesian(...), cylindrical(...) or spherical(...)
        node_radius, blend: joint spheres and joint rounding (struts)
        skin_blend: rounds the lattice-to-skin joints '''
    cellobj = _need_cell(cell, 'lattice', default=lambda: cell_periodic('gyroid'))
    graded = _graded_size(cell_size)
    if cellobj.family == 'shape':
        given = [n for n, v in (('thickness', thickness), ('radius', radius), ('density', density), ('offset', offset),
                                ('wall', wall), ('node_radius', node_radius)) if v is not None]
        if given:
            raise ValueError('lattice: a cell_custom(region, geometry) cell is the geometry you modelled, so {} does not '
                             'apply: give the geometry the thickness you want'.format(', '.join(given)))
        if graded is not None:
            lat = _graded_field(body, graded, lambda c: _periodic(cellobj, c, cell_map))
        else:
            lat = _periodic(cellobj, cell_size, cell_map)
        return fill(body, lat, skin=skin, region=region, depth=depth, blend=skin_blend)
    is_tpms = cellobj.family == 'tpms'
    is_planar = cellobj.family == 'planar'

    def one_size(size_of_cell):
        ''' The infinite lattice made of cells of one size (a number or (sx, sy, sz)) '''
        cell = _cell3(size_of_cell)
        # the member size: given, or from a density
        size = None
        if is_tpms:
            size = offset if style == 'network' else thickness
        elif is_planar:
            size = wall if wall is not None else thickness
        else:
            size = _beam_radius('lattice', thickness, radius, None)
        if density is not None:
            if size is not None:
                raise ValueError('lattice: give either density or the member size, not both')
            if isinstance(density, Shape):
                size = _density_map(cellobj, cell, style)(density)
            else:
                size = lattice_parameter_for_density(cellobj, cell, density, style)
        if size is None:
            size = 0.0 if (is_tpms and style == 'network') else (min(cell) * 0.1)

        if is_tpms:
            if style == 'network':
                return tpms(cellobj, cell, style='network', offset=size, cell_map=cell_map)
            return tpms(cellobj, cell, thickness=size, style=style, cell_map=cell_map)
        if is_planar:
            return planar_lattice(cellobj, cell[0], wall=size, axis=axis, cell_map=cell_map)
        return strut_lattice(cellobj, cell, radius=size, node_radius=node_radius, blend=blend, cell_map=cell_map)

    if graded is not None:
        lat = _graded_field(body, graded, one_size)
    else:
        lat = one_size(10.0 if cell_size is None else cell_size)
    return fill(body, lat, skin=skin, region=region, depth=depth, blend=skin_blend)


lattice = returns_lattice(lattice)


def _graded_size(cell_size):
    ''' The cell size when it is a field (one for all three directions), else None '''
    if isinstance(cell_size, Shape):
        return cell_size
    if isinstance(cell_size, (tuple, list)) and any(isinstance(c, Shape) for c in cell_size):
        raise TypeError('lattice: a cell size that is a field is one field for all three directions, not (sx, sy, sz)')
    return None


_GRADED_MAX_LEVELS = 5


def _graded_field(body, size, build):
    ''' A lattice whose cells are as big as the field `size` says wherever they are.  Cells that tile are of one size, so
        the lattice is made at sizes a factor 2 apart -- from the smallest the field asks for to the largest, found
        by sampling it over the body -- and the field blends the two sizes next to its value, each with a weight that
        falls linearly from 1 to 0 over the factor 2 (the weights add up to 1).  `build(c)` makes the lattice of
        cells of size c. '''
    from fieldes.stdlib.fields import _shape_bounds, field_range
    lo, hi = _shape_bounds(Shape.wrap(body), None)
    smin, smax = field_range(size, body=body, lower=lo, upper=hi, n=20)
    if not smin > 0:
        raise ValueError('lattice: the cell size field must be positive everywhere in the body (it is {:g} somewhere)'
                         .format(smin))
    levels = 1 + max(1, int(math.ceil(math.log2(smax / smin) - 1e-9)))
    if levels > _GRADED_MAX_LEVELS:
        raise ValueError('lattice: the cell size field goes from {:g} to {:g} mm, more than a factor of {}: grade it less'
                         .format(smin, smax, 2 ** (_GRADED_MAX_LEVELS - 1)))
    top = smin * 2.0 ** (levels - 1)
    s = size.max(smin).min(top)
    u = (s / smin).log() * (1.0 / math.log(2.0))            # where the size is, in levels: 0 .. levels - 1
    terms = []
    for j in range(levels):
        w = (1.0 - abs(u - j)).max(0)
        terms.append(build(smin * 2.0 ** j) * w)
    out = terms[0]
    for t in terms[1:]:
        out = out + t
    return out


################################################################################
# Custom cells (like nTop's custom unit cells): any geometry in a box, made by cell_custom(region, geometry);
# your own strut cell from nodes and beams, made by cell_custom_truss(); your own TPMS equation, made by
# cell_custom_tpms()

class UnitCell(LatticeCell):
    ''' A custom strut unit cell: beams between nodes in the unit cube
        (coordinates 0..1 across the cell, scaled by cell_size when used).
        What cell_custom_truss(nodes, beams, mirror) makes; use it wherever a cell is
        taken: lattice(body, cell), strut_lattice(cell), lattice_surface_conform(...).

        nodes: {name: (x, y, z)} or a list of (x, y, z) (then names are the
            indices)
        beams: pairs of node names (or indices); a third entry sets that
            beam's own radius in mm, e.g. ('c', 'v0', 1.2) -- beams without
            one take the lattice's radius
        mirror: 'x', 'xy', 'xyz' ... copies the beams mirrored across the
            cell's mid-planes (x -> 1 - x ...), so you only draw one part of a
            symmetric cell

        UnitCell.check() lists what would make the lattice fall apart or not
        tile: nodes outside the cell, beams of zero length, nodes on a cell
        face without a partner on the opposite face (the neighbouring cell
        has nothing to connect to there), and loose ends inside the cell. '''

    family = 'strut'
    name = 'custom'

    def __init__(self, nodes, beams, mirror=''):
        if isinstance(nodes, dict):
            self.nodes = {k: tuple(float(c) for c in v) for k, v in nodes.items()}
        else:
            self.nodes = {i: tuple(float(c) for c in v) for i, v in enumerate(nodes)}
        segs = []
        for b in beams:
            if len(b) not in (2, 3):
                raise ValueError('UnitCell: a beam is (a, b) or (a, b, radius), got {!r}'.format(b))
            for n in b[:2]:
                if n not in self.nodes:
                    raise ValueError('UnitCell: beam {!r} names an unknown node {!r}'.format(b, n))
            r = b[2] if len(b) == 3 else None
            segs.append((self.nodes[b[0]], self.nodes[b[1]], r))
        for axis in str(mirror).lower():
            i = 'xyz'.index(axis)
            refl = lambda p: tuple(1.0 - p[k] if k == i else p[k] for k in range(3))
            segs += [(refl(a), refl(b), r) for a, b, r in segs]
        # (duplicates from mirroring a beam that lies on the mid-plane)
        seen, self.segments = set(), []
        for a, b, r in segs:
            key = (_canon((a, b)), r)
            if key not in seen:
                seen.add(key)
                self.segments.append((a, b, r))

    def __call__(self):
        return [(a, b) for a, b, _ in self.segments]

    def radii(self):
        ''' The beams that carry their own radius: {beam: radius in mm} '''
        return {_canon((a, b)): r for a, b, r in self.segments if r is not None}

    def check(self):
        ''' What would make the lattice fall apart or not tile (a list of texts; empty when the cell is fine) '''
        problems = []
        tol = 1e-9
        for a, b, _ in self.segments:
            for p in (a, b):
                if any(c < -tol or c > 1 + tol for c in p):
                    problems.append('node {} is outside the cell (0..1)'.format(p))
            if sum((a[k] - b[k]) ** 2 for k in range(3)) < 1e-18:
                problems.append('beam at {} has zero length'.format(a))
        ends = collections.Counter(p for a, b, _ in self.segments for p in (a, b))
        pts = set(ends)
        for p in sorted(pts):
            on = [k for k in range(3) if abs(p[k]) < 1e-9 or abs(p[k] - 1) < 1e-9]
            for k in on:
                q = tuple(1.0 - p[j] if j == k else p[j] for j in range(3))
                q = tuple(round(c, 9) for c in q)
                if not any(all(abs(q[j] - r[j]) < 1e-9 for j in range(3)) for r in pts):
                    problems.append('node {} is on a cell face but nothing meets it on the '
                                    'opposite face ({}): the cells won\'t connect there'.format(p, q))
            if not on and ends[p] == 1:
                problems.append('node {} is a loose end inside the cell (one beam)'.format(p))
        return problems

    def __repr__(self):
        return 'UnitCell({} beams)'.format(len(self.segments))


def _periodic(cell, cell_size=None, cell_map=None):
    ''' A cell_custom(region, geometry) cell repeated through space: the cell (the geometry cut off by the region) is
        scaled to cell_size (the size of the region when None) and repeated on the cell map -- cartesian, cylindrical,
        spherical -- by folding the lattice coordinates (u, v, w) back into the region. '''
    c = cell.size if cell_size is None else _cell3(cell_size)
    if cell.check:
        mism = _periodic_mismatch(cell)
        if mism:
            import warnings
            warnings.warn('cell_custom: the cell does not tile -- {}'.format(mism), stacklevel=3)
    m = cell_map or CellMap()
    u = m.map((X(), Y(), Z()), c)
    # (u % c is the position in the cell in mm; the region's extent per mm of cell is how far into the region that is)
    q = [cell.lo[i] + (u[i] % c[i]) * (cell.size[i] / c[i]) for i in range(3)]
    return cell.solid().remap(q[0], q[1], q[2])


def _periodic_mismatch(cell, n=9):
    ''' Where the geometry's inside/outside differs between opposite faces of the region (sampled on an n x n grid);
        '' if it tiles '''
    from fieldes.stdlib.fields import evaluate
    bad = []
    for axis in range(3):
        o = [k for k in range(3) if k != axis]
        lo_face, hi_face = [], []
        for i in range(n):
            for j in range(n):
                p0 = [0.0, 0.0, 0.0]
                p0[o[0]] = cell.lo[o[0]] + (i + 0.5) / n * cell.size[o[0]]
                p0[o[1]] = cell.lo[o[1]] + (j + 0.5) / n * cell.size[o[1]]
                p1 = list(p0)
                p0[axis], p1[axis] = cell.lo[axis], cell.hi[axis]
                lo_face.append(tuple(p0))
                hi_face.append(tuple(p1))
        a = evaluate(cell.geometry, lo_face)
        b = evaluate(cell.geometry, hi_face)
        wrong = sum(1 for u, v in zip(a, b) if (u < 0) != (v < 0))
        if wrong:
            bad.append('{} of {} points differ between the {} = {:g} and {} = {:g} faces'.format(
                wrong, n * n, 'xyz'[axis], cell.lo[axis], 'xyz'[axis], cell.hi[axis]))
    return '; '.join(bad)


def _region_box(region, who='cell_custom'):
    ''' The box a region is: ((x0, y0, z0), (x1, y1, z1)), found from its field and made exact -- or an error that says
        what a region must be.  A cell tiles space only as a box. '''
    from fieldes.stdlib.fields import evaluate, find_extent
    found = find_extent(region)
    if found is None:
        raise ValueError('{}: the region must be a box (box(...)): the extent of this shape '
                         'could not be found, it is empty or not closed'.format(who))
    lo0, hi0 = found
    size0 = max(hi0[i] - lo0[i] for i in range(3))
    if not size0 > 0:
        raise ValueError('{}: the region has no volume'.format(who))
    mid = [(lo0[i] + hi0[i]) / 2 for i in range(3)]
    # the faces: along each axis through the middle, between the middle (inside) and a little beyond the box found
    # (outside), halve until the field changes sign
    out = 0.01 * size0
    ends = []
    for axis in range(3):
        for side in (-1, 1):
            far = list(mid)
            far[axis] = (lo0[axis] - out) if side < 0 else (hi0[axis] + out)
            ends.append((axis, side, far))
    if evaluate(region, [mid])[0] >= 0:
        raise ValueError('{}: the region must be a box (its middle is not inside it)'.format(who))
    if any(v <= 0 for v in evaluate(region, [e[2] for e in ends])):
        raise ValueError('{}: the region must be a closed box (it is open on a side)'.format(who))
    near = [list(mid) for _ in ends]
    far = [list(e[2]) for e in ends]
    for _ in range(40):
        test = [[(n[k] + f[k]) / 2 for k in range(3)] for n, f in zip(near, far)]
        vals = evaluate(region, test)
        for i, (t, v) in enumerate(zip(test, vals)):
            if v < 0:
                near[i] = t
            else:
                far[i] = t
    lo, hi = list(mid), list(mid)
    for (axis, side, _), n, f in zip(ends, near, far):
        edge = (n[axis] + f[axis]) / 2
        if side < 0:
            lo[axis] = edge
        else:
            hi[axis] = edge
    lo, hi = tuple(lo), tuple(hi)
    # a box: its whole inside is inside the region, and nothing is outside it at the corners
    size = [hi[i] - lo[i] for i in range(3)]
    inside, outside = [], []
    for i in range(4):
        for j in range(4):
            for k in range(4):
                inside.append(tuple(lo[a] + (f + 0.5) / 4 * size[a] for a, f in enumerate((i, j, k))))
    for corner in itertools.product((0.01, 0.99), repeat=3):
        inside.append(tuple(lo[a] + corner[a] * size[a] for a in range(3)))
    for corner in itertools.product((-0.01, 1.01), repeat=3):
        outside.append(tuple(lo[a] + corner[a] * size[a] for a in range(3)))
    if any(v >= 0 for v in evaluate(region, inside)) or any(v <= 0 for v in evaluate(region, outside)):
        raise ValueError('{}: the region must be a box -- a cell that tiles fills the box it is made in; this shape is '
                         'not one (rounded corners, a hole, another shape?)'.format(who))
    return lo, hi


class TPMSEquation(LatticeCell):
    ''' Your own TPMS (or any triply periodic) equation, for tpms() and
        lattice(): f(a, b, c) -> value, where a, b, c are the position in the
        cell as phases (2 pi per cell).  Write it with + - * / and the
        methods .sin() .cos() .sqrt() .square() of a, b, c, e.g. a gyroid:
            cell_custom_tpms(lambda a, b, c: a.sin() * b.cos() + b.sin() * c.cos() + c.sin() * a.cos())
        Its value is turned into a distance in mm (its gradient is followed),
        so thickness= is a real wall thickness. '''

    family = 'tpms'

    def __init__(self, func, name='custom'):
        if not callable(func):
            raise TypeError('tpms_equation: needs a function f(a, b, c)')
        self.func = func
        self.name = name

    def __repr__(self):
        return 'TPMSEquation({})'.format(self.name)


################################################################################
# Cells: what every lattice operation takes

class StrutCell(LatticeCell):
    ''' A standard strut cell that repeats (see cell_periodic) '''
    family = 'strut'

    def __init__(self, key):
        self.key = key
        self.name = key


class TPMSCell(LatticeCell):
    ''' A standard triply periodic minimal surface (see cell_periodic) '''
    family = 'tpms'

    def __init__(self, key):
        self.key = key
        self.name = key


class PlanarCell(LatticeCell):
    ''' A standard planar (2.5D) pattern (see cell_periodic) '''
    family = 'planar'

    def __init__(self, key):
        self.key = key
        self.name = key


class ShapeCell(LatticeCell):
    ''' A cell of any geometry (see cell_custom): a box and a field, the cell is their intersection.
        .region, .geometry   what it was made from
        .lo, .hi, .size      the box the region is, per axis (mm)
        .solid()             the cell on its own, as a shape you can look at '''
    family = 'shape'

    def __init__(self, region, geometry, check=True):
        for what, v in (('the region', region), ('the geometry', geometry)):
            if not isinstance(v, Shape):
                raise TypeError('cell_custom: {} is a shape (a field), not a {}'.format(what, type(v).__name__))
        self.region = region
        self.geometry = geometry
        self.lo, self.hi = _region_box(region)
        self.size = tuple(h - l for l, h in zip(self.lo, self.hi))
        self.check = check
        self.name = 'custom'

    def solid(self):
        ''' The cell on its own: the geometry cut off by the region (their intersection) '''
        s = self.geometry.max(self.region)
        s._bounds = (self.lo, self.hi)
        return s

    def __repr__(self):
        return 'ShapeCell(region {:g} x {:g} x {:g} mm)'.format(*self.size)


def _norm(name):
    return name.lower().replace('-', '_').replace(' ', '_')


def cell_periodic(kind='octet'):
    ''' A standard cell that repeats on a grid -- what a lattice, a conformal lattice or a lattice on a cell map is
        made of.

        kind    a strut cell: cubic, bcc, bccz, fcc, fccz, octet, octahedron, kelvin (= truncated_octahedron),
                diamond_struts, cross, tesseract, cuboctahedron;
                a TPMS: gyroid, schwarz_p, diamond (Schwarz D), neovius, lidinoid, split_p, iwp, frd, fischer_koch_s;
                a planar pattern (2.5D walls): hexagon, triangle, square, kagome

        Only the cell: its size, its thickness (radius, wall, offset) and where it goes are the lattice operation's.
            lattice(part, cell_periodic('gyroid'), cell_size=8, thickness=1.0)
            lattice_surface_conform(part, cell_periodic('truncated_octahedron'), cell_thickness=2, cell_size=5)
        (Custom cells: cell_custom(region, geometry), cell_custom_truss(), cell_custom_tpms().  A foam is made around points,
        not of a cell: voronoi_lattice(random_points(body)).) '''
    if not isinstance(kind, str):
        raise TypeError('cell_periodic(kind): kind is the name of a standard cell, e.g. cell_periodic(\'gyroid\')')
    key = _norm(kind)
    if key in ('voronoi', 'voronoi_foam', 'stochastic', 'delaunay'):
        raise ValueError('cell_periodic: {!r} is not a cell that repeats: a foam is made around points, not of a cell -- '
                         '{}_lattice(random_points(body, spacing=8)) fills the body with it'.format(
                             kind, 'delaunay' if key == 'delaunay' else 'voronoi'))
    if key == 'diamond_struts':
        return StrutCell('diamond')
    if key in TPMS_KINDS:
        return TPMSCell(key)
    if key in PLANAR_KINDS or key in ('honeycomb', 'hex'):
        return PlanarCell('hexagon' if key in ('honeycomb', 'hex') else key)
    if key in STRUT_CELLS:
        return StrutCell(key)
    raise ValueError('unknown cell {!r}. Struts: {}; TPMS: {}; planar: {}'.format(
        kind, ', '.join(sorted(STRUT_CELLS)), ', '.join(sorted(TPMS_KINDS)), ', '.join(PLANAR_KINDS)))


def cell_custom(region=None, geometry=None, check=True, **removed):
    ''' A cell of ANY geometry of your own -- a box for the extent of the cell and a field for what is in it.  The cell
        is the intersection of the two, and it repeats like any periodic cell: on a straight grid, or on a
        cylindrical or spherical cell map.

        region      the box the cell is: box(...) -- any box, its faces are the cell's
                    faces and its size the cell's size
        geometry    the field in it: any shape -- spheres, rods, a TPMS, a boolean of those ... -- that may reach
                    out of the box (only what is inside is the cell)
        check       warn when the cell does not tile: what the geometry does on a face of the region has to be what
                    it does on the opposite face (a rod that leaves through one face must come in through the other,
                    a sphere on a corner must be on all eight)

            cell = cell_custom(box((0, 0, 0), (10, 10, 10)),
                               union(sphere(3, (5, 5, 5)), cylinder(1.0, 20, (5, 5, -5))))
            lattice(part, cell)                                    # cells of 10 mm, as modelled
            lattice(part, cell, cell_size=6)                       # the cell scaled to 6 mm
            lattice(part, cell, cell_map=cylindrical(cells_around=12))

        The cell is yours as modelled: thickness=, radius= or density= do not apply (put the thickness in the
        geometry).  cell.solid() is the cell on its own, to look at it.  (Beams between nodes: cell_custom_truss().
        A triply periodic surface from its equation: cell_custom_tpms().  The arguments nodes, beams, mirror, equation
        and shape of the old cell_custom are gone: `**removed` is only there to say so, and what to write instead.) '''
    if removed:
        old = {'nodes': 'cell_custom_truss(nodes, beams, mirror)', 'beams': 'cell_custom_truss(nodes, beams, mirror)',
               'mirror': 'cell_custom_truss(nodes, beams, mirror)', 'equation': 'cell_custom_tpms(equation)',
               'shape': 'cell_custom(region, geometry): the shape is the geometry, and the box it is modelled in '
                        'the region'}
        hint = '; '.join('{}= is now {}'.format(k, old[k]) for k in removed if k in old)
        raise TypeError('cell_custom(region, geometry) makes a cell of any geometry{}'.format(
            ': ' + hint if hint else ' (it has no argument {})'.format(', '.join(removed))))
    if isinstance(region, (dict, list, tuple)) and geometry is not None and not isinstance(geometry, Shape):
        raise TypeError('cell_custom(region, geometry) makes a cell of any geometry: a box and a field. Beams between '
                        'nodes are cell_custom_truss(nodes, beams)')
    if region is None or geometry is None:
        raise TypeError('cell_custom(region, geometry): a box for the extent of the cell and a field for what is in it')
    return ShapeCell(region, geometry, check)


def cell_custom_truss(nodes, beams, mirror=''):
    ''' A custom strut cell: beams between nodes in the unit cube (coordinates 0..1 across the cell, scaled by
        cell_size when used).

        nodes   {name: (x, y, z)} or a list of (x, y, z) (then the names are the indices)
        beams   pairs of node names (or indices); a third entry gives that beam a radius of its own in mm, e.g.
                ('c', 'v0', 1.2) -- beams without one take the lattice's radius
        mirror  'x', 'xy', 'xyz' ... copies the beams mirrored across the cell's mid-planes (x -> 1 - x ...), so you
                only draw one part of a symmetric cell

            cell = cell_custom_truss({'c': (0.5, 0.5, 0.5), 'o': (0, 0, 0)}, [('c', 'o')], mirror='xyz')
            lattice(part, cell, cell_size=8, radius=0.6)

        (Any other geometry as a cell: cell_custom(region, geometry).)  See UnitCell: .check() lists what would make
        the lattice fall apart or not tile. '''
    if isinstance(nodes, Shape) or beams is None:
        raise TypeError('cell_custom_truss(nodes, beams): beams between nodes in the unit cube. (A cell of any other '
                        'geometry: cell_custom(region, geometry))')
    return UnitCell(nodes, beams, mirror)


def cell_custom_tpms(equation, name='custom'):
    ''' A custom triply periodic surface, from its equation f(a, b, c): a, b, c are the position in the cell as
        phases (2 pi per cell).  Write it with + - * / and the methods .sin() .cos() .sqrt() .square() of a, b, c, e.g.
        a gyroid:

            cell = cell_custom_tpms(lambda a, b, c: a.sin() * b.cos() + b.sin() * c.cos() + c.sin() * a.cos())
            lattice(part, cell, cell_size=8, thickness=0.8)

        Its value is turned into a distance in mm (its gradient is followed), so thickness= is a real wall thickness.
        (Any other geometry as a cell: cell_custom(region, geometry).) '''
    return TPMSEquation(equation, name)


def _need_cell(cell, who, family=None, default=None):
    ''' `cell` as the cell it has to be: a LatticeCell (of this family, if one is asked for) -- and nothing else: not the
        name of a cell, not a list of beams '''
    if cell is None and default is not None:
        return default()
    if not isinstance(cell, LatticeCell):
        hint = ''
        if isinstance(cell, str):
            hint = ': write cell_periodic({!r}) (a standard cell) or cell_custom(...)'.format(cell)
        elif isinstance(cell, PointSet):
            hint = ': points are not a cell -- a foam made around them is voronoi_lattice(points) (or delaunay_lattice(points))'
        raise TypeError('{}: the cell is made by cell_periodic(...), cell_custom(region, geometry), '
                        'cell_custom_truss(...) or cell_custom_tpms(...), not {}{}'
                        .format(who, repr(cell) if isinstance(cell, str) else 'a ' + type(cell).__name__, hint))
    if family is not None and cell.family != family:
        raise ValueError('{}: needs a {} cell, and {!r} is a {} cell'.format(who, family, cell, cell.family))
    return cell


def _plain(cell):
    ''' What the low-level builders (strut_lattice, tpms, planar_lattice) take: the name of a standard cell, or your own
        UnitCell / TPMSEquation '''
    if isinstance(cell, (StrutCell, TPMSCell, PlanarCell)):
        return cell.key
    return cell


################################################################################
# Points: what a foam is made around

class PointSet:
    ''' A set of points: what random_points() makes, and voronoi_lattice() and delaunay_lattice() are made around.  FielDes draws
        them, each as the dot of a gizmo (without the gizmo's handles: they are data, not something to drag).
        .points     the points, (x, y, z) in mm
        .spacing    the average distance between a point and its nearest neighbour that it was made for
        .body       the body they were made in (None when they were not made in one): a lattice made of them fills it
        len(points), iterating it, points[i] work as for a list; points.stats() says how the points really are '''

    _no_gizmo = True             # (not something to drag: FielDes hides the handles button)

    def __init__(self, points, spacing, bounds=None, seed=None, body=None, skin=None, frame=False):
        self.points = [tuple(float(c) for c in p) for p in points]
        self.spacing = float(spacing)
        self.bounds = bounds
        self.seed = seed
        self.body = body
        # (the points that lie ON the surface of the body -- the last `len(skin)` of `points` -- each with the unit normal that points out of the body there: random_points(surface=True))
        self.skin = [(tuple(float(c) for c in p), tuple(float(c) for c in n)) for p, n in (skin or [])]
        # (random_points(surface=True, frame=True): the foam made around them is closed at the surface of the body -- surface: with a net of beams on it, frame: with a full beam on every sharp edge)
        self.frame = bool(frame)
        self.surface = False
        # (point_cloud(): points that were placed, not made at random -- a lattice made of them without a body is clipped by their bounding box)
        self.cloud = False

    def _display(self):
        ''' What FielDes shows for the points: a shape with no extent that carries them (`_markers`), and the viewport draws each of them
            as the dot of a gizmo -- nothing is meshed.  The shape is a constant that depends on the points, so that two sets are two
            shapes (shapes with the same field are one) '''
        shown = self.__dict__.get('_shown')
        if shown is None:
            import zlib
            key = zlib.crc32(repr(self.points).encode('ascii'))
            shown = Shape.wrap(float(1 + key % 1000003))              # (positive everywhere: nothing is inside)
            shown._kind = 'points'
            shown._markers = list(self.points)
            if self.points:
                shown._bounds = (tuple(min(p[i] for p in self.points) for i in range(3)),
                                 tuple(max(p[i] for p in self.points) for i in range(3)))
            self._shown = shown
        return shown

    def __len__(self):
        return len(self.points)

    def __iter__(self):
        return iter(self.points)

    def __getitem__(self, i):
        return self.points[i]

    def stats(self):
        ''' How the points are: {count, mean, min, max} -- the distance from a point to its nearest neighbour (mm), on average, the
            smallest and the largest of them '''
        d = _nearest_distances(self.points, self.spacing * 1.5)
        d = [v for v in d if v is not None]
        if not d:
            return {'count': len(self.points), 'mean': 0.0, 'min': 0.0, 'max': 0.0}
        return {'count': len(self.points), 'mean': sum(d) / len(d), 'min': min(d), 'max': max(d)}

    def __repr__(self):
        s = self.stats()
        return '<PointSet: {count} points, nearest neighbour {mean:.3g} mm on average ({min:.3g} to {max:.3g})>'.format(**s)


def _nearest_distances(points, cell):
    ''' For every point, the distance to the nearest other point (None when there is none within a few cells) '''
    cell = max(float(cell), 1e-9)
    grid = {}
    for i, p in enumerate(points):
        grid.setdefault((int(p[0] // cell), int(p[1] // cell), int(p[2] // cell)), []).append(i)
    out = []
    for i, p in enumerate(points):
        cx, cy, cz = int(p[0] // cell), int(p[1] // cell), int(p[2] // cell)
        best = None
        for ring in (1, 2, 3):
            for dx in range(-ring, ring + 1):
                for dy in range(-ring, ring + 1):
                    for dz in range(-ring, ring + 1):
                        for j in grid.get((cx + dx, cy + dy, cz + dz), ()):
                            if j != i:
                                d = math.dist(p, points[j])
                                if best is None or d < best:
                                    best = d
            if best is not None and best <= ring * cell:
                break
        out.append(best)
    return out


def _mean_nearest(points):
    ''' The average nearest-neighbour distance of a list of points (their spacing) '''
    pts = [tuple(float(c) for c in p) for p in points]
    if len(pts) < 2:
        return 1.0
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    cell = max((max(hi[i] - lo[i] for i in range(3)) / max(1.0, len(pts) ** (1.0 / 3.0))) * 1.5, 1e-6)
    d = [v for v in _nearest_distances(pts, cell) if v is not None]
    return sum(d) / len(d) if d else 1.0


class _Hash:
    ''' A grid of points: asks whether any lies within a distance (cells of the size of the largest distance asked) '''

    def __init__(self, cell):
        self.cell = float(cell)
        self.cells = {}

    def key(self, p):
        c = self.cell
        return (int(p[0] // c), int(p[1] // c), int(p[2] // c))

    def add(self, p):
        self.cells.setdefault(self.key(p), []).append(p)

    def any_within(self, p, r, other=False):
        ''' Whether a point lies within r of p (other: not p itself) '''
        r2 = r * r
        cx, cy, cz = self.key(p)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for q in self.cells.get((cx + dx, cy + dy, cz + dz), ()):
                        if other and q is p:
                            continue
                        if (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 + (q[2] - p[2]) ** 2 <= r2:
                            return True
        return False


def _bcc_points(lo, hi, nearest):
    ''' The points of a body-centred grid over the box whose nearest neighbours are `nearest` apart (every point has the same eight) '''
    c = 2.0 * nearest / math.sqrt(3.0)
    n = [int(math.ceil((hi[i] - lo[i]) / c)) + 1 for i in range(3)]
    if 2.0 * n[0] * n[1] * n[2] > 4e6:
        raise ValueError('random_points: the spacing is too small for this region (it would be more than a million points): make it '
                         'larger, or the region smaller')
    out = []
    for i in range(n[0]):
        for j in range(n[1]):
            for k in range(n[2]):
                x, y, z = lo[0] + i * c, lo[1] + j * c, lo[2] + k * c
                out.append((x, y, z))
                out.append((x + c / 2, y + c / 2, z + c / 2))
    return out


def _inside(body, pts, band):
    ''' The points whose value in the body is below `band` (inside it, or within `band` mm outside) '''
    if body is None:
        return list(pts)
    from fieldes.stdlib.fields import evaluate
    out = []
    for start in range(0, len(pts), 40000):
        chunk = pts[start:start + 40000]
        vals = evaluate(body, chunk)
        out.extend(p for p, v in zip(chunk, vals) if v < band)
    return out


def _skin_points(body, spacing, seed, bounds):
    ''' Points ON the surface of a body about `spacing` apart, each with the unit normal that points out of the body there: [(point, normal), ...].  Random places near the surface are put onto it along the gradient
        of the field, and thinned so that none is nearer than 0.85 of a spacing to another one.  A point is where the surface is under it: none is pulled onto an edge or a corner (the user, 2026-10-27: "the cylinder uses
        the edge pulling that I said should be removed by default") '''
    from fieldes.stdlib.fields import evaluate
    region = _region_of(body, bounds)
    lo = (region.X.lower, region.Y.lower, region.Z.lower)
    hi = (region.X.upper, region.Y.upper, region.Z.upper)
    rng = random.Random(int(seed))
    c = float(spacing)
    n = [max(1, int(math.ceil((hi[i] - lo[i]) / c))) for i in range(3)]
    if n[0] * n[1] * n[2] > 4e6:
        raise ValueError('random_points: the spacing is too small for this region (the surface would need more than four million cells to be found): make it larger, or the region smaller')
    centres = [(lo[0] + (i + 0.5) * c, lo[1] + (j + 0.5) * c, lo[2] + (k + 0.5) * c) for i in range(n[0]) for j in range(n[1]) for k in range(n[2])]
    vals = []
    for start in range(0, len(centres), 40000):
        vals.extend(evaluate(body, centres[start:start + 40000]))
    cands = []
    for q, v in zip(centres, vals):
        if abs(v) < 0.9 * c:                                # (half the diagonal of a cell is 0.87 of its side)
            for _ in range(12):
                cands.append((q[0] + rng.uniform(-0.5 * c, 0.5 * c), q[1] + rng.uniform(-0.5 * c, 0.5 * c), q[2] + rng.uniform(-0.5 * c, 0.5 * c)))
    h = 0.01 * spacing
    for _ in range(5):                                      # (onto the surface: steps along the gradient)
        moved = []
        for q, (v, g, slope) in zip(cands, _values_and_normals(body, cands, h)):
            if g is None or not slope > 1e-9:
                moved.append(q)
            else:
                s = v / slope
                moved.append((q[0] - s * g[0], q[1] - s * g[1], q[2] - s * g[2]))
        cands = moved
    pool = [(q, g) for q, (v, g, slope) in zip(cands, _values_and_normals(body, cands, h)) if g is not None and slope > 1e-9 and abs(v) / slope < 1e-3 * spacing]
    rng.shuffle(pool)
    d = 0.85 * spacing
    near = _Hash(d)
    out = []
    for q, g in pool:
        if not near.any_within(q, d):
            near.add(q)
            out.append((q, g))
    return out


def _values_and_normals(body, pts, h):
    ''' The value of the body's field at each point, the unit gradient there (central differences of step h; None where there is none), and the slope (the length of the gradient) '''
    from fieldes.stdlib.fields import evaluate
    probes = []
    for p in pts:
        x, y, z = p
        probes.extend(((x, y, z), (x + h, y, z), (x - h, y, z), (x, y + h, z), (x, y - h, z), (x, y, z + h), (x, y, z - h)))
    v = []
    for start in range(0, len(probes), 42000):
        v.extend(evaluate(body, probes[start:start + 42000]))
    out = []
    for i in range(len(pts)):
        b = 7 * i
        g = (v[b + 1] - v[b + 2], v[b + 3] - v[b + 4], v[b + 5] - v[b + 6])
        m = math.sqrt(g[0] ** 2 + g[1] ** 2 + g[2] ** 2)
        out.append((v[b], (g[0] / m, g[1] / m, g[2] / m) if m > 1e-12 else None, m / (2.0 * h)))
    return out


def random_points(body=None, spacing=8.0, randomness=0.5, min_distance=None, max_distance=None, seed=1, bounds=None, surface=False, frame=False):
    ''' Points in a body -- with the say over how random they are.  A foam is made around them: voronoi_lattice(points).

        body          where the points are made: inside it, and a layer of one `spacing` outside, so that the cells at its surface are
                      closed (with surface=True there is no layer outside: the boundary is the surface).  Without a body, give
                      bounds=((x0, y0, z0), (x1, y1, z1))
        spacing       how far a point is from its nearest neighbour, on average (mm): the points are made so that they come out that far
                      apart whatever the randomness (points.stats() says how far they really are)
        randomness    0 to 1: how much the points are moved off a regular grid -- 0 is a regular body-centred grid (every cell the same),
                      1 is as random as the distances below allow
        min_distance  no two points are closer than this (mm).  Default: `spacing` less 60 % of the randomness -- a low number lets the
                      points clump, `spacing` itself makes them even
        max_distance  no point is farther than this from its nearest neighbour (mm): a point that is more alone than that is given a
                      neighbour.  Default: none.  It cannot be less than min_distance.  (min_distance and max_distance are the range the
                      distances to the nearest neighbour are in: the closer together they are, the more regular the points)
        seed          another number is another pattern (the same number is the same points again)
        surface       True puts a set of points ON the surface of the body, about `spacing` apart, that are cells of their own, and the foam is closed by the surface: nothing is cut off, and the surface is a net of
                      beams that wraps round the sharp edges -- no beam runs along an edge (that is what frame is for).  The points follow the surface and nothing else: none is pulled onto an edge or a corner.  Needs a body
        frame         True gives the lattice made of the points a frame: a full beam along every sharp edge of the body, found from the body itself (the outline of each of its flat faces; a smooth fillet is no sharp
                      edge).  The points and the cells stay as they are -- the plain foam, or the foam of surface -- and the body cuts them, the frame with them (clip); with clip=False the foam is as wild
                      as it is without a frame, and the frame is whole: its beams lie on the edges, half of each stands out of the body.  Needs a body

        Returns a PointSet, which FielDes draws in the viewport (each point as the dot of a gizmo): len(points), points.stats() (the
        distances the points really have), points.body (the body they were made in), and

            points = random_points(part, spacing=8, randomness=0.7, min_distance=5, max_distance=11)
            foam = voronoi_lattice(points, thickness=1.0)
            truss = delaunay_lattice(points, thickness=1.0)

        make a foam and a stochastic truss around them, in the same body. '''
    if not isinstance(spacing, numbers.Number) or isinstance(spacing, bool) or not spacing > 0:
        raise ValueError('random_points: spacing is a number of mm, more than 0')
    spacing = float(spacing)
    if not isinstance(randomness, numbers.Number) or not 0.0 <= randomness <= 1.0:
        raise ValueError('random_points: randomness is between 0 (a regular grid) and 1 (as random as the distances allow)')
    if min_distance is None:
        min_distance = spacing * (1.0 - 0.6 * randomness)
    if not min_distance > 0 or min_distance > spacing * 1.0000001:
        raise ValueError('random_points: min_distance is more than 0 and at most the spacing ({:g} mm): the nearest neighbour is '
                         '{:g} mm away on average, so no two points can be farther apart than that all through'.format(spacing, spacing))
    if max_distance is not None and max_distance < min_distance:
        raise ValueError('random_points: max_distance ({:g}) cannot be less than min_distance ({:g})'.format(max_distance, min_distance))
    if body is None:
        if bounds is None:
            raise ValueError('random_points: give the body the points are made in, or bounds=((x0, y0, z0), (x1, y1, z1))')
        lo, hi = (tuple(float(c) for c in bounds[0]), tuple(float(c) for c in bounds[1]))
    else:
        body = Shape.wrap(body)
        region = _region_of(body, bounds)
        lo = (region.X.lower, region.Y.lower, region.Z.lower)
        hi = (region.X.upper, region.Y.upper, region.Z.upper)
    # The layer of one spacing outside the body that has points, so that the cells at its surface are closed -- unless the surface is asked for: then the boundary IS the surface, the points are
    # in the body and on its surface, and none stands outside it, cut off by it (the user, 2026-10-27: "the random points still creates points outside of the actual body ... This should be different
    # when choosing surface, because then the boundary is the surface")
    band = 0.0 if surface else spacing
    plo = tuple(c - band for c in lo)
    phi = tuple(c + band for c in hi)

    skin = []
    if surface or frame:
        if body is None:
            raise ValueError('random_points: surface=True and frame=True are made at the surface of the body: give the body the points are made in')
    if surface:
        skin = _skin_points(body, spacing, seed, bounds)

    def make(grid_spacing):
        rng = random.Random(int(seed))
        # 1. a regular grid, moved off it by the randomness, and only where the body is
        jitter = 0.5 * spacing * float(randomness)
        grid = _bcc_points(plo, phi, grid_spacing)
        if jitter > 0:
            grid = [(x + rng.uniform(-jitter, jitter), y + rng.uniform(-jitter, jitter), z + rng.uniform(-jitter, jitter))
                    for x, y, z in grid]
        grid = _inside(body, grid, band)
        if randomness > 0:
            rng.shuffle(grid)
        # 2. no two closer than min_distance
        kept = []
        near = _Hash(min_distance)
        for p, _n in skin:
            near.add(p)
        for p in grid:
            if not near.any_within(p, min_distance * 0.9999999):
                near.add(p)
                kept.append(p)
        # 3. a point that has no neighbour within max_distance is given one, between min_distance and max_distance away
        if max_distance is not None:
            far = _Hash(max_distance)
            for p in kept:
                far.add(p)
            for p, _n in skin:
                far.add(p)
            for _ in range(4):
                lonely = [p for p in kept if not far.any_within(p, max_distance, other=True)]
                if not lonely:
                    break
                tries = []
                for p in lonely:
                    for _k in range(8):
                        d = min_distance + (max_distance - min_distance) * (0.2 + 0.6 * rng.random())
                        while True:
                            v = (rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1))
                            n = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
                            if n > 1e-9:
                                break
                        tries.append((p, (p[0] + d * v[0] / n, p[1] + d * v[1] / n, p[2] + d * v[2] / n)))
                good = set(_inside(body, [q for _, q in tries], band))
                done = set()
                for p, q in tries:
                    if id(p) in done or q not in good or near.any_within(q, min_distance * 0.9999999):
                        continue
                    done.add(id(p))
                    near.add(q)
                    far.add(q)
                    kept.append(q)
                if not done:
                    break
        return kept

    # (the grid is spaced so that the points come out `spacing` from their nearest neighbour on average: moving points off the grid, and
    # taking out the ones that are too close, bring them nearer, so the grid is made coarser until they are that far apart)
    grid_spacing = spacing
    kept = make(grid_spacing)
    for _ in range(4 if randomness > 0 and len(kept) > 8 else 0):
        mean = _mean_nearest(kept)
        if abs(mean - spacing) <= 0.03 * spacing or not mean > 0:
            break
        grid_spacing *= (spacing / mean) ** 0.95
        kept = make(grid_spacing)
    if len(kept) < 5:
        raise ValueError('random_points: only {} points: the spacing is too large for this region'.format(len(kept)))
    out = PointSet(kept + [p for p, _n in skin], spacing, (lo, hi), seed, body, skin, frame)
    out.surface = bool(surface)
    return out


random_points._makes = 'points'


def _bounding_box(points, spacing):
    ''' The box that just holds the points, as a body (an axis they all share the place on is given a thin side, a quarter of the spacing each way, so that the box has some thickness) '''
    lo = [min(p[i] for p in points) for i in range(3)]
    hi = [max(p[i] for p in points) for i in range(3)]
    for i in range(3):
        if hi[i] - lo[i] < 1e-6 * max(1.0, spacing):
            lo[i] -= 0.25 * spacing
            hi[i] += 0.25 * spacing
    return sys.modules['fieldes.stdlib'].box(tuple(lo), tuple(hi))


def point_cloud(points, body=None):
    ''' Points of your own, exactly where you put them -- nothing random.  A foam or a truss is made around them as around random_points(): voronoi_lattice(cloud), delaunay_lattice(cloud).

        points   the points: places (x, y, z), or the points of the model tree (point(x, y, z): its dot is dragged by its gizmo, to put it exactly where it is wanted).  A lattice needs at least five
        body     the body that clips the lattice (and that it fills): any body.  Without one, the bounding box of the points

            cloud = point_cloud([(0, 0, 0), (10, 0, 0), (0, 10, 0), (10, 10, 0), (5, 5, 8), (5, 5, -8)])
            foam = voronoi_lattice(cloud) '''
    if isinstance(points, (str, bytes)) or not hasattr(points, '__iter__'):
        raise TypeError('point_cloud: the points are a list of places (x, y, z) or of points: point_cloud([point_1, point_2, ...])')
    places = []
    for p in list(points):
        place = getattr(p, '_point_pos', None)
        if place is None:
            try:
                place = tuple(float(c) for c in p)
            except (TypeError, ValueError):
                place = None
        if place is None or len(place) != 3 or not all(math.isfinite(c) for c in place):
            raise ValueError('point_cloud: every point is a place (x, y, z) or a point whose place is known, and {!r} is not'.format(p))
        places.append(place)
    if len(places) < 5:
        raise ValueError('point_cloud: {} points -- a foam or a truss is made around at least five'.format(len(places)))
    if body is not None:
        body = Shape.wrap(body)
    out = PointSet(places, _mean_nearest(places), None, None, body)
    out.cloud = True
    return out


point_cloud._makes = 'points'


def _as_points(who, points):
    ''' `points` as a PointSet: one from random_points(), or a list of (x, y, z) '''
    if isinstance(points, PointSet):
        return points
    if isinstance(points, Shape):
        raise TypeError('{}: a lattice of this kind is made around POINTS, not of a body: give it random_points(body, ...) (or a list of '
                        '(x, y, z)) -- {}(random_points(body, spacing=8))'.format(who, who))
    if isinstance(points, (list, tuple)) and len(points) >= 5 and all(isinstance(p, (list, tuple)) and len(p) == 3 for p in points):
        return PointSet(points, _mean_nearest(points))
    raise TypeError('{}: the first argument is a set of points: random_points(body, ...), or a list of at least five (x, y, z)'.format(who))


def _flat_planes(body, skin, spacing):
    ''' The flat faces of the body, found from the points of its skin: [(unit normal, offset)], the plane  normal . x = offset  lying in the surface.  The candidates are the planes that several points of the
        skin lie on with the same normal; one is a face when the surface lies in it all round them (four places a little way off in the plane have the value 0 in the field of the body) '''
    from fieldes.stdlib.fields import evaluate
    groups = {}
    for p, n in skin:
        groups.setdefault((round(n[0], 2), round(n[1], 2), round(n[2], 2)), []).append((p, n, n[0] * p[0] + n[1] * p[1] + n[2] * p[2]))
    cands = []
    for members in groups.values():
        members.sort(key=lambda m: m[2])
        clusters, run = [], [members[0]]
        for m in members[1:]:
            if m[2] - run[-1][2] > 0.1 * spacing:
                clusters.append(run)
                run = [m]
            else:
                run.append(m)
        clusters.append(run)
        for cl in clusters:
            if len(cl) < 2:
                continue
            mean = tuple(sum(m[1][i] for m in cl) / len(cl) for i in range(3))
            size = math.sqrt(mean[0] ** 2 + mean[1] ** 2 + mean[2] ** 2)
            n = (mean[0] / size, mean[1] / size, mean[2] / size)
            centre = tuple(sum(m[0][i] for m in cl) / len(cl) for i in range(3))
            a = (0.0, 0.0, 1.0) if abs(n[2]) < 0.9 else (1.0, 0.0, 0.0)
            t1 = (n[1] * a[2] - n[2] * a[1], n[2] * a[0] - n[0] * a[2], n[0] * a[1] - n[1] * a[0])
            m1 = math.sqrt(t1[0] ** 2 + t1[1] ** 2 + t1[2] ** 2)
            t1 = (t1[0] / m1, t1[1] / m1, t1[2] / m1)
            t2 = (n[1] * t1[2] - n[2] * t1[1], n[2] * t1[0] - n[0] * t1[2], n[0] * t1[1] - n[1] * t1[0])
            step = _PLANE_PROBE * spacing
            probes = [tuple(centre[i] + s * step * t[i] for i in range(3)) for t in (t1, t2) for s in (1.0, -1.0)]
            cands.append((n, n[0] * centre[0] + n[1] * centre[1] + n[2] * centre[2], probes))
    vals = evaluate(body, [q for _n, _c, probes in cands for q in probes]) if cands else []
    planes = []
    for k, (n, c, _probes) in enumerate(cands):
        if all(abs(v) < 0.05 * spacing for v in vals[4 * k:4 * k + 4]):
            if not any(abs(n[0] - m[0]) + abs(n[1] - m[1]) + abs(n[2] - m[2]) < 0.03 and abs(c - d) < 0.1 * spacing for m, d in planes):
                planes.append((n, c))
    return planes


_WALL_BAND = 0.12               # (how far outside the surface, in spacings, a node of the foam still counts as a node of the surface: of a body whose faces are all flat)
_ROUND_FLAT_BAND = 0.25         # (of a body with a round face: outside a flat face of it ...)
_ROUND_BAND = 0.3               # (... and outside a round one)
_USE_PATCHES = True             # (a cell is mirrored across the surface patches that rays from it find, too)
_REACH = 2.5                    # (a cell that is no farther than this, in spacings, from a face is mirrored across it)
_PLANE_PROBE = 0.7              # (the four places that show a face is flat all round a group of skin points are this far, in spacings, from the middle of the group)


def _surface_patches(body, sites, reach, spacing):
    ''' The surfaces of the body that are near each site, as planes [(unit normal that points out, offset)] -- one list for each site: rays are cast from it in 26 directions as far as `reach`, and where one meets the surface the
        plane is the plane that touches the surface there.  Planes that are alike (normals within 6 degrees, offsets within 0.15 of a spacing) are one; the three that were met nearest are kept, and of them only those that
        support the body (no site is beyond them) '''
    from fieldes.stdlib.fields import evaluate
    dirs = []
    for i in (-1, 0, 1):
        for j in (-1, 0, 1):
            for k in (-1, 0, 1):
                if i or j or k:
                    m = math.sqrt(i * i + j * j + k * k)
                    dirs.append((i / m, j / m, k / m))
    steps = 6
    step = reach / steps
    probes = [(q[0] + s * step * d[0], q[1] + s * step * d[1], q[2] + s * step * d[2]) for q in sites for d in dirs for s in range(steps + 1)]
    vals = []
    for start in range(0, len(probes), 40000):
        vals.extend(evaluate(body, probes[start:start + 40000]))
    hits = []
    for si, q in enumerate(sites):
        for di, d in enumerate(dirs):
            base = (si * len(dirs) + di) * (steps + 1)
            for s in range(1, steps + 1):
                a, b = vals[base + s - 1], vals[base + s]
                if a < 0.0 <= b:
                    t = (s - 1 + (-a) / (b - a)) * step
                    inside = max(0.0, t - 0.04 * spacing)
                    hits.append((si, t, (q[0] + inside * d[0], q[1] + inside * d[1], q[2] + inside * d[2])))
                    break
    out = [[] for _ in sites]
    if not hits:
        return out
    info = _values_and_normals(body, [h[2] for h in hits], 0.01 * spacing)
    found = [[] for _ in sites]
    for (si, t, p), (v, g, slope) in zip(hits, info):
        if g is None or not slope > 1e-9:
            continue
        foot = (p[0] - v / slope * g[0], p[1] - v / slope * g[1], p[2] - v / slope * g[2])
        found[si].append((t, g, g[0] * foot[0] + g[1] * foot[1] + g[2] * foot[2]))
    for si, plan in enumerate(found):
        for t, g, c in sorted(plan, key=lambda x: x[0]):
            if len(out[si]) >= 3:
                break
            if not any(g[0] * m[0] + g[1] * m[1] + g[2] * m[2] > 0.9945 and abs(c - d) < 0.15 * spacing for m, d in out[si]):
                out[si].append((g, c))
    # (only a plane that supports the body: no point of the foam is beyond it -- a plane through an edge, that cuts the body, is none)
    for si in range(len(sites)):
        out[si] = [(g, c) for g, c in out[si] if all(g[0] * s[0] + g[1] * s[1] + g[2] * s[2] - c <= 0.05 * spacing for s in sites)]
    return out


def _without_islands(beams, smallest=3):
    ''' The beams, less every piece of the graph that is fewer than `smallest` beams and touches nothing else '''
    parent = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for a, b in beams:
        parent[find(a)] = find(b)
    count = {}
    for a, b in beams:
        r = find(a)
        count[r] = count.get(r, 0) + 1
    return [bm for bm in beams if count[find(bm[0])] >= smallest]


def _normal_basis(n):
    ''' Two unit vectors that lie in the plane whose unit normal is n '''
    a = (0.0, 0.0, 1.0) if abs(n[2]) < 0.9 else (1.0, 0.0, 0.0)
    t1 = (n[1] * a[2] - n[2] * a[1], n[2] * a[0] - n[0] * a[2], n[0] * a[1] - n[1] * a[0])
    m = math.sqrt(t1[0] ** 2 + t1[1] ** 2 + t1[2] ** 2)
    t1 = (t1[0] / m, t1[1] / m, t1[2] / m)
    t2 = (n[1] * t1[2] - n[2] * t1[1], n[2] * t1[0] - n[0] * t1[2], n[0] * t1[1] - n[1] * t1[0])
    return t1, t2


def _point_segment(p, a, b):
    ''' The distance from the point p to the segment a-b '''
    ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    d2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]
    t = 0.0 if d2 < 1e-18 else max(0.0, min(1.0, ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1] + (p[2] - a[2]) * ab[2]) / d2))
    return math.sqrt((p[0] - a[0] - t * ab[0]) ** 2 + (p[1] - a[1] - t * ab[1]) ** 2 + (p[2] - a[2] - t * ab[2]) ** 2)


def _simplify(points, tol, closed=False):
    ''' The indices of the points of a polyline (a loop when closed) that are left when it is made as short as it can be with no point farther than tol from it (Douglas and Peucker) '''
    n = len(points)
    if n < 3:
        return list(range(n))
    keep = [False] * n
    if closed:
        far = max(range(n), key=lambda i: math.dist(points[0], points[i]))
        keep[0] = keep[far] = True
        stack = [(0, far), (far, n)]
    else:
        keep[0] = keep[n - 1] = True
        stack = [(0, n - 1)]
    while stack:
        a, b = stack.pop()
        pa, pb = points[a], points[b % n]
        best, at = -1.0, None
        for i in range(a + 1, b):
            d = _point_segment(points[i % n], pa, pb)
            if d > best:
                best, at = d, i
        if at is not None and best > tol:
            keep[at % n] = True
            stack.append((a, at))
            stack.append((at, b))
    return [i for i in range(n) if keep[i]]


def _face_outline(body, n, c, spacing, lo, hi):
    ''' The outline of the flat face that lies in the plane  n . x = c : [(points, closed)], polylines.  The plane is looked at on a grid a tenth of a spacing across, and a place belongs to the face where the field of the body is
        0 (it is 0 all over a face, and more than 0 beyond it, less than 0 where the plane goes into the body); the outline is the border of that, found as the contour of the grid (each crossing of a line of the grid put exactly
        onto the border by bisection), and then made as short as it can be with none of its points more than 0.02 of a spacing off '''
    from fieldes.stdlib.fields import evaluate
    t1, t2 = _normal_basis(n)
    o = (n[0] * c, n[1] * c, n[2] * c)
    us, vs = [], []
    for x in (lo[0], hi[0]):
        for y in (lo[1], hi[1]):
            for z in (lo[2], hi[2]):
                d = (x - o[0], y - o[1], z - o[2])
                us.append(d[0] * t1[0] + d[1] * t1[1] + d[2] * t1[2])
                vs.append(d[0] * t2[0] + d[1] * t2[1] + d[2] * t2[2])
    h = 0.1 * spacing
    while (math.ceil((max(us) - min(us)) / h) + 5) * (math.ceil((max(vs) - min(vs)) / h) + 5) > 300000:
        h *= 1.25
    nu = int(math.ceil((max(us) - min(us)) / h)) + 5
    nv = int(math.ceil((max(vs) - min(vs)) / h)) + 5
    u0, v0 = min(us) - 2 * h, min(vs) - 2 * h

    def at(u, v):
        return (o[0] + u * t1[0] + v * t2[0], o[1] + u * t1[1] + v * t2[1], o[2] + u * t1[2] + v * t2[2])

    def values(pts):
        out = []
        for start in range(0, len(pts), 40000):
            out.extend(evaluate(body, pts[start:start + 40000]))
        return out

    pts = [at(u0 + i * h, v0 + j * h) for j in range(nv) for i in range(nu)]
    tol = 0.02 * spacing
    ins = [abs(v) <= tol for v in values(pts)]
    if not any(ins) or all(ins):
        return []
    edges = []
    for j in range(nv):
        for i in range(nu):
            k = j * nu + i
            if i + 1 < nu and ins[k] != ins[k + 1]:
                edges.append((('h', i, j), k, k + 1))
            if j + 1 < nv and ins[k] != ins[k + nu]:
                edges.append((('v', i, j), k, k + nu))
    a_pts = [pts[ka] if ins[ka] else pts[kb] for _key, ka, kb in edges]          # (the end of an edge of the grid that is in the face ...)
    b_pts = [pts[kb] if ins[ka] else pts[ka] for _key, ka, kb in edges]          # (... and the end that is not)
    lo_t, hi_t = [0.0] * len(edges), [1.0] * len(edges)
    for _ in range(12):
        mids = []
        for q in range(len(edges)):
            m, a, b = 0.5 * (lo_t[q] + hi_t[q]), a_pts[q], b_pts[q]
            mids.append((a[0] + m * (b[0] - a[0]), a[1] + m * (b[1] - a[1]), a[2] + m * (b[2] - a[2])))
        for q, v in enumerate(values(mids)):
            m = 0.5 * (lo_t[q] + hi_t[q])
            if abs(v) <= tol:
                lo_t[q] = m
            else:
                hi_t[q] = m
    cross = {}
    for q, (key, _ka, _kb) in enumerate(edges):
        m, a, b = 0.5 * (lo_t[q] + hi_t[q]), a_pts[q], b_pts[q]
        cross[key] = (a[0] + m * (b[0] - a[0]), a[1] + m * (b[1] - a[1]), a[2] + m * (b[2] - a[2]))
    adj = {}

    def link(p, q):
        adj.setdefault(p, []).append(q)
        adj.setdefault(q, []).append(p)

    for j in range(nv - 1):
        for i in range(nu - 1):
            f0, f1, f2, f3 = ins[j * nu + i], ins[j * nu + i + 1], ins[(j + 1) * nu + i + 1], ins[(j + 1) * nu + i]
            bot, rgt, top, lft = ('h', i, j), ('v', i + 1, j), ('h', i, j + 1), ('v', i, j)
            e = []
            if f0 != f1:
                e.append(bot)
            if f1 != f2:
                e.append(rgt)
            if f3 != f2:
                e.append(top)
            if f0 != f3:
                e.append(lft)
            if len(e) == 2:
                link(e[0], e[1])
            elif len(e) == 4:                                  # (a cell with the face in two opposite corners: each corner is cut off by itself)
                if f0:
                    link(bot, lft)
                    link(top, rgt)
                else:
                    link(bot, rgt)
                    link(top, lft)
    seen, out = set(), []
    for start in [k for k, v in adj.items() if len(v) == 1] + list(adj):
        if start in seen:
            continue
        chain, cur = [start], start
        seen.add(start)
        while True:
            nxt = next((w for w in adj[cur] if w not in seen), None)
            if nxt is None:
                break
            chain.append(nxt)
            seen.add(nxt)
            cur = nxt
        closed = len(chain) > 2 and chain[0] in adj[chain[-1]]
        line = [cross[k] for k in chain]
        out.append(([line[i] for i in _simplify(line, tol, closed)], closed))
    return out


def _frame_graph(body, spacing, seed, bounds):
    ''' The frame of a body: a graph of the beams along its sharp edges, or None when it has none.  The edges are found from the body itself: the flat faces of it are found from points on its surface, and the outline of each
        is a chain of edges -- unless the surface goes on past the outline with the same direction, give or take 35 degrees (the edge of a face that a round one runs into smoothly, a fillet, is no sharp edge).  An edge between
        two flat faces is found twice, once from each: one is kept.  An edge between two round faces is not found '''
    region = _region_of(body, bounds)
    lo, hi = (region.X.lower, region.Y.lower, region.Z.lower), (region.X.upper, region.Y.upper, region.Z.upper)
    fine = 0.5 * spacing
    try:
        skin = _skin_points(body, fine, seed, bounds)
    except ValueError:
        fine = spacing
        skin = _skin_points(body, spacing, seed, bounds)
    faces = []
    for n, c in _flat_planes(body, skin, fine):
        on = [p for p, _nn in skin if abs(n[0] * p[0] + n[1] * p[1] + n[2] * p[2] - c) < 0.05 * fine]
        if len(on) < 3:
            continue
        t1, t2 = _normal_basis(n)
        mean = [sum(p[i] for p in on) / len(on) for i in range(3)]
        su = sum(((p[0] - mean[0]) * t1[0] + (p[1] - mean[1]) * t1[1] + (p[2] - mean[2]) * t1[2]) ** 2 for p in on) / len(on)
        sv = sum(((p[0] - mean[0]) * t2[0] + (p[1] - mean[1]) * t2[1] + (p[2] - mean[2]) * t2[2]) ** 2 for p in on) / len(on)
        suv = sum(((p[0] - mean[0]) * t1[0] + (p[1] - mean[1]) * t1[1] + (p[2] - mean[2]) * t1[2]) * ((p[0] - mean[0]) * t2[0] + (p[1] - mean[1]) * t2[1] + (p[2] - mean[2]) * t2[2]) for p in on) / len(on)
        small = 0.5 * (su + sv) - math.sqrt(max(0.0, 0.25 * (su + sv) ** 2 - (su * sv - suv * suv)))
        if math.sqrt(max(small, 0.0)) >= 0.3 * fine:        # (the points on a plane that only touches a round face lie along a line)
            faces.append((len(on), n, c))
    faces.sort(key=lambda f: -f[0])
    pieces = []                                              # (beam: two points, the normal of the face)
    for _count, n, c in faces[:24]:
        for line, closed in _face_outline(body, n, c, spacing, lo, hi):
            pairs = [(line[i], line[i + 1]) for i in range(len(line) - 1)] + ([(line[-1], line[0])] if closed and len(line) > 2 else [])
            pieces.extend((p, q, n) for p, q in pairs if math.dist(p, q) > 1e-6)
    if not pieces:
        return None
    # (a sharp edge: the surface just beyond the outline, in the plane of the face, has another direction -- put onto the surface and looked at there)
    reach = 0.15 * spacing
    outs = []
    for p, q, n in pieces:
        d = (q[0] - p[0], q[1] - p[1], q[2] - p[2])
        w = (n[1] * d[2] - n[2] * d[1], n[2] * d[0] - n[0] * d[2], n[0] * d[1] - n[1] * d[0])
        size = math.sqrt(w[0] ** 2 + w[1] ** 2 + w[2] ** 2)
        m = (0.5 * (p[0] + q[0]), 0.5 * (p[1] + q[1]), 0.5 * (p[2] + q[2]))
        w = (w[0] / size, w[1] / size, w[2] / size)
        outs.append((m, w))
    from fieldes.stdlib.fields import evaluate
    side = []
    for m, w in outs:
        side.append((m[0] + reach * w[0], m[1] + reach * w[1], m[2] + reach * w[2]))
        side.append((m[0] - reach * w[0], m[1] - reach * w[1], m[2] - reach * w[2]))
    sv = []
    for start in range(0, len(side), 40000):
        sv.extend(evaluate(body, side[start:start + 40000]))
    probes, which = [], []
    for k in range(len(pieces)):
        a, b = sv[2 * k], sv[2 * k + 1]
        if max(abs(a), abs(b)) <= 0.02 * spacing:
            which.append(False)                              # (the face goes on on both sides of the outline: no edge)
            probes.append(side[2 * k])
        else:
            which.append(True)
            probes.append(side[2 * k] if abs(a) >= abs(b) else side[2 * k + 1])
    unit = 0.01 * spacing
    for _ in range(5):
        moved = []
        for q, (v, g, slope) in zip(probes, _values_and_normals(body, probes, unit)):
            if g is None or not slope > 1e-9:
                moved.append(q)
            else:
                moved.append((q[0] - v / slope * g[0], q[1] - v / slope * g[1], q[2] - v / slope * g[2]))
        probes = moved
    normals = [g for _v, g, _slope in _values_and_normals(body, probes, unit)]
    sharp = []
    for (p, q, n), ok, g in zip(pieces, which, normals):
        if ok and g is not None and n[0] * g[0] + n[1] * g[1] + n[2] * g[2] < 0.82:
            sharp.append((p, q))
    nodes, beams, index, kept = [], [], {}, []
    grain = 0.02 * spacing
    for p, q in sharp:
        if any(_point_segment(p, a, b) < 0.08 * spacing and _point_segment(q, a, b) < 0.08 * spacing for a, b in kept):
            continue                                        # (an edge between two flat faces, found from the other one)
        kept.append((p, q))
        ids = []
        for x in (p, q):
            key = tuple(int(round(v / grain)) for v in x)
            if key not in index:
                index[key] = len(nodes)
                nodes.append(x)
            ids.append(index[key])
        if ids[0] != ids[1]:
            beams.append((ids[0], ids[1]))
    return LatticeGraph(nodes, beams) if beams else None


def _edge_beams(edges, r, blend):
    ''' The frame, as a body of its own: its beams, with the box that holds them '''
    lat = edges.thicken(r, blend)
    pad = float(r) if isinstance(r, numbers.Number) else 0.0
    lat._bounds = (tuple(min(p[i] for p in edges.nodes) - pad for i in range(3)), tuple(max(p[i] for p in edges.nodes) + pad for i in range(3)))
    return lat


def _plus_edges(lat, edges, r, blend):
    ''' The lattice with the frame added to it, whole (nothing cuts it) '''
    if edges is None:
        return lat
    return sys.modules['fieldes.stdlib'].union(lat, _edge_beams(edges, r, blend))


def _beam_kinds(body, place, walls, beams, spacing):
    ''' What each beam of the closed foam is: 0 inside the body, 1 in the surface (a net on a face), 2 along a sharp edge (and for those the two normals of the surface on its two sides, as {beam: (n1, n2)}): (kinds, edges).  A beam is in the surface when both its ends are and its middle is.  It is along a sharp edge when
        the surface has two different normals on the two sides of it: eight points a little way off its middle, put onto the surface, have normals that are more than 35 degrees apart (a flat face has one normal, a round
        one turns by a few degrees over so short a way) '''
    from fieldes.stdlib.fields import evaluate
    unit = 0.01 * spacing
    kinds = [0] * len(beams)
    cand = [k for k, (a, b) in enumerate(beams) if a in walls and b in walls]
    if not cand:
        return kinds, {}
    mids = [tuple(0.5 * (place[beams[k][0]][c] + place[beams[k][1]][c]) for c in range(3)) for k in cand]
    vals = []
    for start in range(0, len(mids), 40000):
        vals.extend(evaluate(body, mids[start:start + 40000]))
    on = [(k, m) for k, m, v in zip(cand, mids, vals) if abs(v) < 0.2 * spacing]
    for k, _m in on:
        kinds[k] = 1
    if not on:
        return kinds, {}
    reach = 0.15 * spacing
    turns = [(math.cos(i * math.pi / 4), math.sin(i * math.pi / 4)) for i in range(8)]
    probes = []
    for k, m in on:
        a, b = place[beams[k][0]], place[beams[k][1]]
        t = [b[c] - a[c] for c in range(3)]
        size = math.sqrt(sum(x * x for x in t))
        t = [1.0, 0.0, 0.0] if size < 1e-12 else [x / size for x in t]
        h0 = (1.0, 0.0, 0.0) if abs(t[0]) < 0.9 else (0.0, 1.0, 0.0)
        e1 = [t[1] * h0[2] - t[2] * h0[1], t[2] * h0[0] - t[0] * h0[2], t[0] * h0[1] - t[1] * h0[0]]
        s1 = math.sqrt(sum(x * x for x in e1))
        e1 = [x / s1 for x in e1]
        e2 = [t[1] * e1[2] - t[2] * e1[1], t[2] * e1[0] - t[0] * e1[2], t[0] * e1[1] - t[1] * e1[0]]
        for ca, sa in turns:
            probes.append(tuple(m[c] + reach * (ca * e1[c] + sa * e2[c]) for c in range(3)))
    for _ in range(5):                                      # (onto the surface: steps along the gradient)
        moved = []
        for q, (v, g, slope) in zip(probes, _values_and_normals(body, probes, unit)):
            if g is None or not slope > 1e-9:
                moved.append(q)
            else:
                s = v / slope
                moved.append((q[0] - s * g[0], q[1] - s * g[1], q[2] - s * g[2]))
        probes = moved
    normals = [g for _v, g, _slope in _values_and_normals(body, probes, unit)]
    edges = {}
    for j, (k, _m) in enumerate(on):
        ns = [n for n in normals[8 * j:8 * j + 8] if n is not None]
        low, pair = 1.0, None
        for i in range(len(ns)):
            for q in range(i + 1, len(ns)):
                d = ns[i][0] * ns[q][0] + ns[i][1] * ns[q][1] + ns[i][2] * ns[q][2]
                if d < low:
                    low, pair = d, (ns[i], ns[q])
        if low < 0.82:
            kinds[k] = 2
            edges[k] = pair
    return kinds, edges


def _voronoi_skeleton(pts, body, keep='all'):
    ''' The graph of a Voronoi foam that the surface of the body closes, as a Voronoi lattice of a body is made (the user, 2026-10-27: "have you never seen anyone use Voronoi in nTop? That is exactly what it
        should be like", "I told you to not clip it off"): the cells are the cells of the points -- the points inside the body, and the points of the skin, which are cells that lie against the surface -- and every cell near the surface has its mirror image across the surface, so that it is closed there and nothing is cut off.  The beams are the edges of the cells
        that are inside the body or in the surface (where the cells meet it: a Voronoi net of beams over the surface).  The walls are the surface itself: the beams lie on it, and nothing cuts them.
        A flat face is mirrored in (the planes of the faces are found from the skin), a round one by the nearest surface of the field; near an edge a cell is mirrored across each face and across both.
        keep says which beams are kept: 'all' (the whole foam), 'surface' (without the beams that run along a sharp edge: the net on the surface wraps round them), 'frame' (without the net on the faces: only the
        beams in the surface that run along a sharp edge, and the foam inside) '''
    import itertools
    from fieldes.stdlib.fields import evaluate
    spacing = pts.spacing
    depth = 0.0                                             # (the walls are the surface itself)
    own = pts.points[:len(pts.points) - len(pts.skin)]
    reach = _REACH * spacing
    # 1. the cells: the points, and the points of the skin a little inside the surface (their cells lie against it)
    sites = list(own)
    for p, n in pts.skin:
        s = depth + 0.1 * spacing
        sites.append((p[0] - s * n[0], p[1] - s * n[1], p[2] - s * n[2]))
    skin = pts.skin or _skin_points(body, spacing, pts.seed, pts.bounds)
    planes = _flat_planes(body, skin, spacing)
    # (a body whose faces are all flat -- every point of its skin is in one of the planes -- is closed by those planes alone)
    rounded = any(not any(abs(m[0] * p[0] + m[1] * p[1] + m[2] * p[2] - d) < 0.05 * spacing for m, d in planes) for p, _nn in skin)
    unit = 0.01 * spacing
    seen = {tuple(int(round(c / unit)) for c in q) for q in sites}
    ghosts = []

    def add(q):
        k = tuple(int(round(c / unit)) for c in q)
        if k not in seen:
            seen.add(k)
            ghosts.append(q)

    # 2. their mirror images: across each flat face a cell is near (and across two or three of them in turn, near an edge or a corner) ...
    patches = _surface_patches(body, sites, reach, spacing) if (_USE_PATCHES and rounded) else [[] for _ in sites]
    for q, extra in zip(sites, patches):
        near = [(n, c) for n, c in planes if -reach < n[0] * q[0] + n[1] * q[1] + n[2] * q[2] - (c - depth) < 0.0]
        for n, c in extra:
            if not any(n[0] * m[0] + n[1] * m[1] + n[2] * m[2] > 0.9945 and abs(c - d) < 0.15 * spacing for m, d in near):
                near.append((n, c))
                break                                       # (one more: the nearest surface that is new -- the round face that a flat one meets)
        for size in range(1, min(3, len(near)) + 1):
            for order in itertools.permutations(near, size):
                g = q
                for n, c in order:
                    d = n[0] * g[0] + n[1] * g[1] + n[2] * g[2] - (c - depth)
                    g = (g[0] - 2.0 * d * n[0], g[1] - 2.0 * d * n[1], g[2] - 2.0 * d * n[2])
                add(g)
    # ... and across the nearest surface of the field, which is what a round face is mirrored in
    for q, (v, n, slope) in zip(sites, _values_and_normals(body, sites, unit)):
        if n is None or not slope > 1e-9:
            continue
        d = -v / slope - depth
        if 0.0 < d < reach:
            add((q[0] + 2.0 * d * n[0], q[1] + 2.0 * d * n[1], q[2] + 2.0 * d * n[2]))
    g = points_graph(sites + ghosts, 'voronoi')
    nodes = g.nodes
    f = []
    for start in range(0, len(nodes), 40000):
        f.extend(evaluate(body, nodes[start:start + 40000]))
    # 3. the beams between nodes that are inside the body or in the wall (the surface).  The cells of a round face are closed by planes that touch the surface, and their nodes lie a little OUTSIDE it (up to
    # 0.34 mm in a cylinder of 8): a node of the wall is put ON the surface, by a few steps along the gradient of the field, so that the foam has the shape of the body; a node farther out than the wall is left out
    band = (_ROUND_FLAT_BAND if rounded else _WALL_BAND) * spacing
    far = max(band, (_ROUND_BAND if rounded else _WALL_BAND) * spacing)
    deep = [v < -depth - 0.1 * spacing for v in f]
    wall = [i for i, v in enumerate(f) if not deep[i] and v <= -depth + far]
    place = [list(p) for p in nodes]
    for _ in range(4):
        if not wall:
            break
        for i, (v, n, slope) in zip(wall, _values_and_normals(body, [tuple(place[i]) for i in wall], unit)):
            if n is None or not slope > 1e-9:
                continue
            step = (v + depth) / slope
            place[i] = [place[i][c] - step * n[c] for c in range(3)]
    # (a node that lies outside the band of a flat face is kept only when, put onto the surface, it is where no flat face is)
    wall = [i for i in wall if f[i] <= -depth + band or not any(abs(n[0] * place[i][0] + n[1] * place[i][1] + n[2] * place[i][2] - c) < 0.03 * spacing for n, c in planes)]
    walls = set(wall)
    ok = [deep[i] or i in walls for i in range(len(nodes))]
    # nodes that land on one place (at an edge, where two walls meet) are one node: a beam of no length is no beam
    weld, rep = {}, list(range(len(nodes)))
    grain = 0.02 * spacing
    for i in range(len(nodes)):
        if ok[i]:
            rep[i] = weld.setdefault(tuple(int(round(v / grain)) for v in place[i]), i)
    beams = sorted({(min(rep[a], rep[b]), max(rep[a], rep[b])) for a, b in g.beams if ok[a] and ok[b] and rep[a] != rep[b]})
    if keep != 'all' and beams:
        kinds, _edges = _beam_kinds(body, place, walls, beams, spacing)
        drop = 2 if keep == 'surface' else 1
        beams = _without_islands([bm for bm, kind in zip(beams, kinds) if kind != drop])
    if not beams:
        raise ValueError('the points make no beams: there are too few of them')
    used = sorted({n for beam in beams for n in beam})
    remap = {n: k for k, n in enumerate(used)}
    return LatticeGraph([tuple(place[n]) for n in used], [(remap[a], remap[b]) for a, b in beams])


def _points_lattice(who, style, points, thickness, radius, density, skin, blend, skin_blend, body, clip):
    ''' The beams of the Voronoi or Delaunay graph of the points that are in the body, as a body '''
    pts = _as_points(who, points)
    if body is not None and pts.body is not None:
        raise ValueError('{}: these points were made in a body, and the lattice fills that one -- to fill another, make the points in it: '
                         'random_points(other, ...)'.format(who))
    body = pts.body if body is None else body
    if body is None and pts.cloud:
        body = _bounding_box(pts.points, pts.spacing)
    if body is None:
        raise ValueError('{}: a list of points does not say which body to fill: {}(points, body=part) -- or make the points in the body '
                         'with random_points(part, ...), which knows it'.format(who, who))
    body = Shape.wrap(body)
    r = _beam_radius(who, thickness, radius, None)
    if density is not None and r is not None:
        raise ValueError("{}: give either density or the beams' size (thickness or radius), not both".format(who))
    if r is None and density is None:
        r = 0.08 * pts.spacing
    closed = style == 'voronoi' and pts.surface
    if closed:
        g = _voronoi_skeleton(pts, body, 'surface')
    else:
        g = _graph_in_body(points_graph(pts.points, style), body, pts.spacing)
    edges = _frame_graph(body, pts.spacing, pts.seed, pts.bounds) if pts.frame else None       # (the frame is its own lattice: it does not touch the points or the cells)
    if density is not None:
        # beams of radius r fill about pi r^2 * (length inside) / volume
        from fieldes.stdlib.fields import volume_of
        region = _region_of(body, pts.bounds)
        lo = (region.X.lower, region.Y.lower, region.Z.lower)
        hi = (region.X.upper, region.Y.upper, region.Z.upper)
        vol = volume_of(body, lo, hi)
        target = float(density)
        r = math.sqrt(target * vol / (math.pi * (sum(g.lengths()) + (sum(edges.lengths()) if edges is not None else 0.0))))
        # refine against the measured density (joints overlap, beams stick out)
        for _ in range(4):
            got = volume_of(fill(body, _plus_edges(g.thicken(r), edges, r, 0.0)), lo, hi) / vol
            if got <= 0:
                break
            r *= math.sqrt(target / got)
    lat = _plus_edges(g.thicken(r, blend), edges, r, blend)       # (the frame is part of the lattice: the body cuts it as it cuts the rest)
    solid_skin = isinstance(skin, numbers.Number) and skin > 0
    if not clip and not solid_skin:
        # (clip=False: the lattice is left whole -- the beams on the surface and on the edges stand half out of the body, as in nTop, and the user clips them if they want to; the user, 2026-10-28)
        b = getattr(body, '_bounds', None)
        if b:
            pad = (0.0 if closed else 0.5 * pts.spacing) + (float(r) if isinstance(r, numbers.Number) else 0.0)
            lat._bounds = (tuple(c - pad for c in b[0]), tuple(c + pad for c in b[1]))
        return lat
    guarded = sys.modules['fieldes.stdlib'].fill
    return guarded(body, lat, skin=skin, blend=skin_blend)       # (the guarded fill: the locked places of an excluded body stay)


def voronoi_lattice(points, thickness=None, radius=None, density=None, skin=0.0, blend=0.0, skin_blend=0.0, body=None, clip=True):
    ''' A foam: the edges of the Voronoi cells around a set of points, as round beams, filling the body the points were made in.  With points made with surface=True (random_points(body, surface=True)) the points on the surface are cells of their own, the foam is closed by the surface and the surface is a net of beams that wraps round the sharp
        edges; with frame=True every sharp edge of the body is a full beam, added to the foam whatever it is (the points and the cells are not changed by it).  The body cuts the lattice, the frame with it, unless clip=False
        (then it is whole, and the beams on the surface and on the edges stand half out of the body).

        points     random_points(body, ...) -- how far apart they are, how random, the nearest and the farthest two may be -- or a list of
                   (x, y, z), which does not know a body: then say it with body=
        thickness  the diameter of the beams, mm: a number, or a field.  radius= is the same thing, half of it.  Default: 16 % of the
                   spacing of the points
        density    instead of the beams' size: the share of the body that is material (0..1), calibrated
        skin       the thickness of a solid skin on the surface of the body (0: none; a skin is joined to the foam cut by the body)
        blend      rounds the joints; skin_blend rounds the joints with the skin
        body       the body to fill -- only for a list of points.  Points from random_points() fill the body they were made in
        clip       True (the default) cuts the foam by the body, so that nothing of it stands outside it: a beam that the body cuts ends flat (with frame or surface the cut lies one beam radius outside the
                   surface, so that the beams of the net on it are not cut); False leaves the foam whole, its beams ending where the points make them end

            points = random_points(part, spacing=8, randomness=0.7, min_distance=5, max_distance=11)
            foam = voronoi_lattice(points, thickness=1.0) '''
    return _points_lattice('voronoi_lattice', 'voronoi', points, thickness, radius, density, skin, blend, skin_blend, body, clip)


def delaunay_lattice(points, thickness=None, radius=None, density=None, skin=0.0, blend=0.0, skin_blend=0.0, body=None, clip=True):
    ''' A stochastic truss: beams along the Delaunay edges between a set of points (every point joined to its neighbours), filling the
        body the points were made in.  The arguments are those of voronoi_lattice(): the points, the beams' size, the skin, clip ...

            points = random_points(part, spacing=8)
            truss = delaunay_lattice(points, thickness=1.0) '''
    return _points_lattice('delaunay_lattice', 'delaunay', points, thickness, radius, density, skin, blend, skin_blend, body, clip)


voronoi_lattice = returns_lattice(voronoi_lattice)
delaunay_lattice = returns_lattice(delaunay_lattice)


def _graph_in_body(g, body, spacing):
    ''' The beams of a graph that are in the body (or within half a spacing of it): the rest are dropped.  Five places along each are looked at '''
    from fieldes.stdlib.fields import evaluate
    nodes, beams = g.nodes, g.beams
    if not beams:
        raise ValueError('the points make no beams: there are too few of them')
    samples = []
    for a, b in beams:
        for s in range(5):
            t = s / 4.0
            samples.append(tuple(nodes[a][i] * (1 - t) + nodes[b][i] * t for i in range(3)))
    vals = []
    for start in range(0, len(samples), 50000):
        vals.extend(evaluate(body, samples[start:start + 50000]))
    kept = [beams[i] for i in range(len(beams)) if min(vals[5 * i:5 * i + 5]) < 0.5 * spacing]
    if not kept:
        raise ValueError('no beams inside the body: the points are not where the body is')
    used = sorted({n for beam in kept for n in beam})
    remap = {n: k for k, n in enumerate(used)}
    return LatticeGraph([nodes[n] for n in used], [(remap[a], remap[b]) for a, b in kept])


################################################################################
# Graph (beam) lattices: arbitrary graphs, Voronoi foams, surface lattices

class LatticeGraph:
    ''' A graph of nodes and beams: nodes [(x, y, z), ...], beams [(i, j), ...].
        .thicken(radius, blend) makes it solid; radius may be a number, a
        list (one per node) or a field. '''

    def __init__(self, nodes, beams):
        self.nodes = [tuple(float(c) for c in p) for p in nodes]
        self.beams = [(int(a), int(b)) for a, b in beams]

    def lengths(self):
        ''' The length of every beam (mm) '''
        return [math.dist(self.nodes[a], self.nodes[b]) for a, b in self.beams]

    def __repr__(self):
        L = self.lengths()
        mean = sum(L) / len(L) if L else 0.0
        return '<LatticeGraph: {} nodes, {} beams, mean beam length {:.3g} mm>'.format(
            len(self.nodes), len(self.beams), mean)

    def thicken(self, radius=0.5, blend=0.0):
        ''' The graph as round beams of `radius` mm: see graph_lattice() '''
        return graph_lattice(self.nodes, self.beams, radius, blend)


_SPREAD = 1.4


def _graph_from_c(ptr, what):
    if not ptr:
        err = lib.libfive_lattice_last_error()
        raise ValueError('{}: {}'.format(what, err.decode('utf-8', 'replace') if err else 'failed'))
    g = ptr.contents
    nodes = [(g.nodes[3 * i], g.nodes[3 * i + 1], g.nodes[3 * i + 2]) for i in range(g.node_count)]
    beams = [(g.beams[2 * i], g.beams[2 * i + 1]) for i in range(g.beam_count)]
    lib.libfive_graph_delete(ptr)
    return LatticeGraph(nodes, beams)


def _need(name):
    if getattr(lib, name, None) is None:
        raise RuntimeError('this FielDes library is too old for graph lattices ({} missing)'.format(name))


def _region_of(body, bounds):
    from fieldes.ffi import libfive_region_t
    if bounds is None:
        bounds = getattr(body, '_bounds', None)
    if bounds is None:
        from fieldes.stdlib.fields import find_extent       # (searched with the script's var() numbers)
        found = find_extent(body)
        if found is None:
            raise ValueError('could not find the extent of the body: pass '
                             'bounds=((x0, y0, z0), (x1, y1, z1))')
        bounds = found
    lo, hi = bounds
    r = libfive_region_t()
    for axis, a, b in zip((r.X, r.Y, r.Z), lo, hi):
        axis.lower, axis.upper = float(a), float(b)
    return r


def _node_radii(nodes, radius):
    if isinstance(radius, Shape):
        from fieldes.stdlib.fields import evaluate
        return [max(0.0, v) for v in evaluate(radius, nodes)]
    if isinstance(radius, numbers.Number):
        return [float(radius)] * len(nodes)
    r = [float(v) for v in radius]
    if len(r) != len(nodes):
        raise ValueError('graph_lattice: one radius per node')
    return r


def graph_lattice(nodes, beams, radius=0.5, blend=0.0):
    ''' Round beams along the edges of any graph: nodes [(x, y, z), ...],
        beams [(i, j), ...] (node indices).  radius: a number, one per node,
        or a field (evaluated at the nodes; each beam tapers linearly
        between its ends).  blend rounds the joints. '''
    _need('libfive_beam_lattice')
    nodes = [tuple(float(c) for c in p) for p in nodes]
    beams = [(int(a), int(b)) for a, b in beams]
    radii = _node_radii(nodes, radius)
    n, m = len(nodes), len(beams)
    if m == 0:
        raise ValueError('graph_lattice: no beams')
    cn = (ctypes.c_float * (3 * n))(*[c for p in nodes for c in p])
    cb = (ctypes.c_int * (2 * m))(*[i for b in beams for i in b])
    cr = (ctypes.c_float * n)(*radii)
    return Shape(lib.libfive_beam_lattice(cn, n, cb, m, cr, float(blend)))


@content_cached('voronoi_graph', limit=6)
def voronoi_graph(body, cell_size=8.0, style='voronoi', relax=2, seed=1, bounds=None):
    ''' A random graph filling a body: Poisson-disk points about cell_size
        apart, joined by the edges of their Voronoi cells (style='voronoi',
        a foam) or by their Delaunay edges (style='delaunay', a stochastic
        truss).  relax: iterations that make the cells more even. '''
    _need('libfive_lattice_volume_graph')
    body = Shape.wrap(body)
    modes = {'voronoi': 1, 'delaunay': 0, 'stochastic': 0}
    if style not in modes:
        raise ValueError("voronoi_graph: style is 'voronoi' or 'delaunay'")
    # (Poisson-disk points `spacing` apart end up ~1.4 spacing from their
    # neighbours: scale so cell_size is the typical cell-to-cell distance)
    ptr = lib.libfive_lattice_volume_graph(body.ptr, _region_of(body, bounds),
                                           float(cell_size) / _SPREAD,
                                           modes[style], int(relax), int(seed) & 0xffffffff)
    return _graph_from_c(ptr, 'voronoi_graph')


@content_cached('surface_graph', limit=6)
def surface_graph(body, cell_size=8.0, pattern='triangle', seed=1, bounds=None):
    ''' A graph on a body's surface: points about cell_size apart joined into
        triangles (pattern='triangle') or the dual cells (pattern='voronoi',
        mostly hexagons) '''
    _need('libfive_lattice_surface_graph')
    body = Shape.wrap(body)
    modes = {'triangle': 0, 'voronoi': 1, 'hexagon': 1}
    if pattern not in modes:
        raise ValueError("surface_graph: pattern is 'triangle' or 'voronoi'")
    ptr = lib.libfive_lattice_surface_graph(body.ptr, _region_of(body, bounds),
                                            float(cell_size) / _SPREAD,
                                            modes[pattern], int(seed) & 0xffffffff)
    return _graph_from_c(ptr, 'surface_graph')


def surface_lattice(body, cell_size=8.0, pattern='voronoi', thickness=None, radius=None, seed=1, blend=0.0, bounds=None):
    ''' A lattice that does not repeat, over the surface of a body: a random net of round beams, one layer thick, the axes of the beams lying ON the surface.

        body       any body (a solid, a lattice ...): the whole of its surface is covered, hollows and edges too
        cell_size  how far apart the nodes of the net are on average (mm)
        pattern    'voronoi': cells that are mostly hexagons, three beams at every node -- or 'delaunay': triangles, six beams at a node on average
        thickness  the diameter of the beams, mm: a number, or a field.  radius= is the same thing, half of it.  Default: 16 % of the cell size
        seed       another number is another net (the same number is the same net again)
        blend      rounds the joints
        bounds     ((x0, y0, z0), (x1, y1, z1)): where to look for the surface, when FielDes cannot find the extent of the body

            net = surface_lattice(part, cell_size=6, thickness=1.0)

        Half of each beam is outside the body: intersect the net with the body to have it inside. '''
    modes = {'voronoi': 'voronoi', 'delaunay': 'triangle'}
    if pattern not in modes:
        raise ValueError("surface_lattice: pattern is 'voronoi' or 'delaunay'")
    if not isinstance(cell_size, numbers.Number) or isinstance(cell_size, bool) or not cell_size > 0:
        raise ValueError('surface_lattice: cell_size is a number of mm, more than 0')
    body = Shape.wrap(body)
    r = _beam_radius('surface_lattice', thickness, radius, 0.08 * float(cell_size))
    out = surface_graph(body, float(cell_size), modes[pattern], seed, bounds).thicken(r, blend)
    region = _region_of(body, bounds)
    pad = float(r) if isinstance(r, numbers.Number) else 0.0
    out._bounds = ((region.X.lower - pad, region.Y.lower - pad, region.Z.lower - pad), (region.X.upper + pad, region.Y.upper + pad, region.Z.upper + pad))
    return out


surface_lattice = returns_lattice(surface_lattice)


@content_cached('points_graph', limit=6)
def points_graph(points, style='delaunay'):
    ''' The Delaunay (or Voronoi) graph of your own points '''
    _need('libfive_lattice_points_graph')
    pts = [tuple(float(c) for c in p) for p in points]
    cp = (ctypes.c_float * (3 * len(pts)))(*[c for p in pts for c in p])
    mode = {'voronoi': 1, 'delaunay': 0}[style]
    return _graph_from_c(lib.libfive_lattice_points_graph(cp, len(pts), mode), 'points_graph')

# What these take, for the menus and the model tree (see fieldes.kinds.takes): a lattice is made in a body, of the cell selected with it, and a field is its thickness; points are made in a
# body; a foam is made around points, and they are all it takes -- they know the body it fills
from fieldes.kinds import takes as _declare, BODIES as _BODIES
lattice._takes = _declare(_BODIES | {'cell', 'field'}, [_BODIES])
random_points._takes = _declare(_BODIES, [_BODIES])
voronoi_lattice._takes = _declare({'points'}, [{'points'}])
delaunay_lattice._takes = _declare({'points'}, [{'points'}])
point_cloud._takes = _declare({'point'}, [{'point'}])
surface_lattice._takes = _declare(_BODIES, [_BODIES])
