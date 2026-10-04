'''
Lattices: periodic TPMS and strut lattices, field-driven thickness, cell
maps (Cartesian, cylindrical, spherical), and filling bodies.

Every lattice operation takes a CELL: the thing the lattice is made of.  Three functions make one --

    cell_periodic(kind)          a standard cell that repeats: a strut cell (octet, bcc, kelvin ...), a TPMS (gyroid,
                                 schwarz_p ...) or a planar pattern (hexagon ...)
    cell_non_periodic(kind)      cells that do not repeat: 'voronoi' (a foam) or 'delaunay' (a stochastic truss)
    cell_custom(...)             your own: nodes and beams, a TPMS equation, or any shape that tiles

-- and lattice(), lattice_surface_conform(), strut_lattice(), tpms() ... take it as their `cell`, and nothing else: a
name such as 'gyroid' is not a cell, cell_periodic('gyroid') is.  The cell is only WHAT the lattice is made of; how thick
it is, how big, and where it goes (a body, a surface, a cell map) are the operation's.

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

    # a foam that does not repeat
    foam = lattice(part, cell_non_periodic('voronoi'), cell_size=8, radius=0.5)

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

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib.content_cache import content_cached

__all__ = ['cell_periodic', 'cell_non_periodic', 'cell_custom', 'LatticeCell',
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
    ''' What a lattice is made of (see the module): made by cell_periodic(), cell_non_periodic() or cell_custom(),
        taken by every lattice operation as its `cell`.
        .family      'strut' (beams between nodes), 'tpms' (a periodic surface), 'planar' (a 2.5D pattern), 'shape'
                     (a shape that tiles) or 'foam' (cells that do not repeat)
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
            'split_p' | 'iwp' | 'frd' | 'fischer_koch_s'), or your own: cell_custom(equation=f)
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
            cell_custom(nodes, beams) (beams may have their own radius)
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
    if blend and blend > 0:
        k = float(blend)
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
    if skin and (not isinstance(skin, numbers.Number) or skin > 0):
        shell = body.max(-(body + skin))
        if blend:
            k = float(blend)
            h = (k - abs(out - shell)).max(0) / k
            out = out.min(shell) - h * h * k / 4
            out = out.max(body)           # (the blend must not grow the body)
        else:
            out = out.min(shell)
    b = getattr(body, '_bounds', None)
    if b:
        out._bounds = b
    return out


def lattice(body, cell=None, cell_size=10.0, thickness=None, radius=None, density=None,
            style='sheet', offset=None, skin=0.0, region='volume', depth=None, cell_map=None,
            node_radius=None, blend=0.0, skin_blend=0.0, wall=None, axis='z'):
    ''' A body filled with a lattice, in one call.

        cell: what it is made of -- cell_periodic(kind) (a TPMS: gyroid, schwarz_p, diamond, neovius, lidinoid, split_p,
            iwp, frd, fischer_koch_s; a strut cell: cubic, bcc, bccz, fcc, fccz, octet, octahedron, kelvin,
            diamond_struts, cross, tesseract, cuboctahedron; a planar pattern: hexagon, triangle, square, kagome),
            cell_non_periodic('voronoi' | 'delaunay') or cell_custom(...).  Default: cell_periodic('gyroid')
        cell_size: mm, or (sx, sy, sz)
        thickness: the member size of every cell -- the wall of a sheet TPMS, the diameter of the beams of a strut
            or non-periodic cell (radius= is the same thing for beams, half of it); offset (network TPMS), wall
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
    cell = _cell3(cell_size)
    if cellobj.family == 'foam':
        r = _beam_radius('lattice', thickness, radius, None)
        if r is None and density is None:
            r = 0.08 * min(cell)
        return _voronoi_lattice(body, min(cell), radius=r, style=cellobj.style, relax=cellobj.relax, seed=cellobj.seed,
                                skin=skin, blend=blend, skin_blend=skin_blend, density=density)
    if cellobj.family == 'shape':
        lat = _periodic(cellobj.shape, cell_size, cell_map, check=cellobj.check)
        return fill(body, lat, skin=skin, region=region, depth=depth, blend=skin_blend)
    is_tpms = cellobj.family == 'tpms'
    is_planar = cellobj.family == 'planar'

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
            lat = tpms(cellobj, cell, style='network', offset=size, cell_map=cell_map)
        else:
            lat = tpms(cellobj, cell, thickness=size, style=style, cell_map=cell_map)
    elif is_planar:
        lat = planar_lattice(cellobj, cell[0], wall=size, axis=axis, cell_map=cell_map)
    else:
        lat = strut_lattice(cellobj, cell, radius=size, node_radius=node_radius, blend=blend,
                            cell_map=cell_map)
    return fill(body, lat, skin=skin, region=region, depth=depth, blend=skin_blend)


################################################################################
# Custom cells (like nTop's custom unit cells), made by cell_custom(): your own strut
# cell from nodes and beams, any shape as a periodic cell, your own TPMS equation

class UnitCell(LatticeCell):
    ''' A strut unit cell of your own: beams between nodes in the unit cube
        (coordinates 0..1 across the cell, scaled by cell_size when used).
        What cell_custom(nodes, beams, mirror) makes; use it wherever a cell is
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


def _periodic(shape, cell_size=10.0, cell_map=None, check=True):
    ''' Any shape as a unit cell: the shape you model in one cell -- the box
        from (0, 0, 0) to cell_size -- repeated through space (along a cell
        map too: cylindrical, spherical ...).  Model the cell so that it
        tiles: what leaves one face must come in on the opposite face (a
        solid that touches a face must touch it the same way on the other
        side).  check=True samples the faces and warns where they don't
        match (the lattice would have steps or holes at every cell
        boundary). '''
    c = _cell3(cell_size)
    shape = Shape.wrap(shape)
    if check:
        mism = _periodic_mismatch(shape, c)
        if mism:
            import warnings
            warnings.warn('cell_custom(shape=): the cell does not tile -- {}'.format(mism), stacklevel=2)
    m = cell_map or CellMap()
    u = m.map((X(), Y(), Z()), c)
    q = [u[i] % c[i] for i in range(3)]
    return shape.remap(q[0], q[1], q[2])


def _periodic_mismatch(shape, c, n=9):
    ''' Where the shape's inside/outside differs between opposite cell faces
        (sampled on an n x n grid); '' if it tiles '''
    bad = []
    for axis in range(3):
        o = [k for k in range(3) if k != axis]
        wrong = 0
        for i in range(n):
            for j in range(n):
                p0 = [0.0, 0.0, 0.0]
                p0[o[0]] = (i + 0.5) / n * c[o[0]]
                p0[o[1]] = (j + 0.5) / n * c[o[1]]
                p1 = list(p0)
                p1[axis] = c[axis]
                if (shape(*p0) < 0) != (shape(*p1) < 0):
                    wrong += 1
        if wrong:
            bad.append('{} of {} points differ between the {} = 0 and {} = {} faces'.format(
                wrong, n * n, 'xyz'[axis], 'xyz'[axis], c[axis]))
    return '; '.join(bad)


class TPMSEquation(LatticeCell):
    ''' Your own TPMS (or any triply periodic) equation, for tpms() and
        lattice(): f(a, b, c) -> value, where a, b, c are the position in the
        cell as phases (2 pi per cell).  Write it with + - * / and the
        methods .sin() .cos() .sqrt() .square() of a, b, c, e.g. a gyroid:
            cell_custom(equation=lambda a, b, c: a.sin() * b.cos() + b.sin() * c.cos()
                                                 + c.sin() * a.cos())
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
    ''' Any shape that tiles, as a cell (see cell_custom): the shape is modelled in the box from (0, 0, 0) to the
        cell size the lattice is made with '''
    family = 'shape'

    def __init__(self, shape, check=True):
        self.shape = Shape.wrap(shape)
        self.check = check
        self.name = 'shape'


class FoamCell(LatticeCell):
    ''' Cells that do not repeat (see cell_non_periodic) '''
    family = 'foam'
    periodic = False

    def __init__(self, style='voronoi', relax=2, seed=1):
        self.style = style
        self.relax = int(relax)
        self.seed = int(seed)
        self.name = style


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
            lattice_surface_conform(part, cell_periodic('truncated_octahedron'), depth=2, cell_size=5)
        (Your own cell: cell_custom().  Cells that do not repeat: cell_non_periodic().) '''
    if not isinstance(kind, str):
        raise TypeError('cell_periodic(kind): kind is the name of a standard cell, e.g. cell_periodic(\'gyroid\')')
    key = _norm(kind)
    if key in ('voronoi', 'voronoi_foam', 'stochastic', 'delaunay'):
        raise ValueError('cell_periodic: {!r} does not repeat: use cell_non_periodic({!r})'.format(kind, kind))
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


def cell_non_periodic(kind='voronoi', relax=2, seed=1):
    ''' Cells that do not repeat: points at random about a cell size apart, joined into a graph that fills the body
        (or lies on a surface).

        kind    'voronoi' (the edges of the Voronoi cells: a foam) or 'delaunay' (the Delaunay edges: a stochastic truss)
        relax   iterations that make the cells more even
        seed    another number is another random pattern

            lattice(part, cell_non_periodic('voronoi'), cell_size=8, radius=0.5) '''
    key = _norm(kind) if isinstance(kind, str) else kind
    if key in ('voronoi', 'voronoi_foam'):
        return FoamCell('voronoi', relax, seed)
    if key in ('delaunay', 'stochastic'):
        return FoamCell('delaunay', relax, seed)
    raise ValueError("cell_non_periodic: kind is 'voronoi' or 'delaunay'")


def cell_custom(nodes=None, beams=None, mirror='', equation=None, shape=None, name='custom', check=True):
    ''' A cell of your own -- one of three kinds:

        struts      cell_custom(nodes, beams, mirror='')   beams between nodes in the unit cube (see UnitCell: the
                    nodes {name: (x, y, z)} or a list, the beams pairs of nodes, optionally with a radius of their own,
                    `mirror` 'x', 'xy', 'xyz' to draw one part of a symmetric cell)
        a surface   cell_custom(equation=f)                f(a, b, c) of the position in the cell as phases (2 pi per
                    cell), written with + - * / and .sin() .cos() .sqrt() .square(); its value becomes a distance in
                    mm, so thickness= is a real wall (see TPMSEquation)
        a shape     cell_custom(shape=s)                   the shape you model in one cell, the box from (0, 0, 0) to
                    the cell size, repeated; model it so that it tiles (check=True warns where the faces do not match)

            cell = cell_custom({'c': (0.5, 0.5, 0.5), 'o': (0, 0, 0)}, [('c', 'o')], mirror='xyz')
            lattice(part, cell, cell_size=8, radius=0.6) '''
    given = [nodes is not None or beams is not None, equation is not None, shape is not None]
    if sum(given) != 1:
        raise ValueError('cell_custom: give nodes and beams, or equation=, or shape= (one of the three)')
    if equation is not None:
        return TPMSEquation(equation, name)
    if shape is not None:
        return ShapeCell(shape, check)
    if nodes is None or beams is None:
        raise ValueError('cell_custom: struts need both nodes and beams')
    return UnitCell(nodes, beams, mirror)


def _need_cell(cell, who, family=None, default=None):
    ''' `cell` as the cell it has to be: a LatticeCell (of this family, if one is asked for) -- and nothing else: not the
        name of a cell, not a list of beams '''
    if cell is None and default is not None:
        return default()
    if not isinstance(cell, LatticeCell):
        hint = ''
        if isinstance(cell, str):
            hint = ': write cell_periodic({!r}) (a standard cell), cell_non_periodic(...) or cell_custom(...)'.format(cell)
        raise TypeError('{}: the cell is made by cell_periodic(...), cell_non_periodic(...) or cell_custom(...), not {}{}'
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
        search = libfive_region_t()
        for axis in (search.X, search.Y, search.Z):
            axis.lower, axis.upper = -1e6, 1e6
        out = libfive_region_t()
        open_sides = ctypes.c_int(0)
        if not lib.libfive_tree_bounds(body.ptr, search, 200000, 2.0, ctypes.byref(out),
                                       ctypes.byref(open_sides)) or open_sides.value:
            raise ValueError('could not find the extent of the body: pass '
                             'bounds=((x0, y0, z0), (x1, y1, z1))')
        return out
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


@content_cached('points_graph', limit=6)
def points_graph(points, style='delaunay'):
    ''' The Delaunay (or Voronoi) graph of your own points '''
    _need('libfive_lattice_points_graph')
    pts = [tuple(float(c) for c in p) for p in points]
    cp = (ctypes.c_float * (3 * len(pts)))(*[c for p in pts for c in p])
    mode = {'voronoi': 1, 'delaunay': 0}[style]
    return _graph_from_c(lib.libfive_lattice_points_graph(cp, len(pts), mode), 'points_graph')


def _voronoi_lattice(body, cell_size=8.0, radius=0.6, style='voronoi', relax=2, seed=1,
                     skin=0.0, blend=0.0, skin_blend=0.0, bounds=None, density=None):
    ''' A body filled with a random Voronoi foam (or, style='delaunay', a
        stochastic truss) of round beams.  radius may be a field (e.g. from
        a regression); seed picks a different random pattern.
        density: a relative density instead of the radius (a number). '''
    body = Shape.wrap(body)
    g = voronoi_graph(body, cell_size, style, relax, seed, bounds)
    if density is not None:
        # beams of radius r fill about pi r^2 * (length inside) / volume
        from fieldes.stdlib.fields import volume_of
        region = _region_of(body, bounds)
        lo = (region.X.lower, region.Y.lower, region.Z.lower)
        hi = (region.X.upper, region.Y.upper, region.Z.upper)
        vol = volume_of(body, lo, hi)
        target = float(density)
        r = math.sqrt(target * vol / (math.pi * sum(g.lengths())))
        # refine against the measured density (joints overlap, beams stick out)
        for _ in range(4):
            got = volume_of(fill(body, g.thicken(r)), lo, hi) / vol
            if got <= 0:
                break
            r *= math.sqrt(target / got)
        radius = r
    lat = g.thicken(radius, blend)
    return fill(body, lat, skin=skin, blend=skin_blend)


def _surface_lattice(body, cell_size=8.0, radius=0.6, pattern='triangle', seed=1, blend=0.0,
                     bounds=None, with_body=None):
    ''' Round beams on a body's surface: a triangle lattice
        (pattern='triangle') or its dual, a Voronoi / hexagon-like pattern
        (pattern='voronoi').  The beams are centred on the surface; pass
        with_body='inside' to keep only their part inside the body, or
        'union' to add them to the body. '''
    body = Shape.wrap(body)
    g = surface_graph(body, cell_size, pattern, seed, bounds)
    lat = g.thicken(radius, blend)
    if with_body == 'inside':
        lat = lat.max(body)
    elif with_body == 'union':
        lat = lat.min(body)
    elif with_body is not None:
        raise ValueError("with_body is None, 'inside' or 'union'")
    b = getattr(body, '_bounds', None)
    if b:
        lat._bounds = b
    return lat
