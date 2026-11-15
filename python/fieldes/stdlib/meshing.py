'''
Meshing: a body as a mesh of elements, for the solver of another program.  Every kind of element and every way of meshing is a function of its own, with only the arguments it uses.

    from fieldes import *

    part = ...                                                   # any body, in mm

    # Volume
    mesh = mesh_tet4(part, element_size=2.0)       # tetrahedra of 4 nodes, about 2 mm, that follow the surface
    mesh = mesh_tet10(part, 2.0)                     # tetrahedra of 10 nodes: a node in the middle of every edge, the edges on the surface curved
    mesh = mesh_hex_quanta(part, 2.0)                # hexahedra only, the cubes of a Cartesian grid: the boundary is a staircase of whole cubes (Salome's Body Fitting with Quanta)
    mesh = mesh_hex20_quanta(part, 2.0)              # the same cubes with a node in the middle of every edge (20 nodes)
    mesh = mesh_cartesian(part, 2.0)                 # hexahedra inside, and the cells the surface cuts out of the cubes at the surface (Salome's Body Fitting)
    mesh = mesh_hex_extrusion(prism, 1.0)            # a body that is a prism along an axis: quadrilaterals swept along it, the surface exact (Salome's Extrusion 3D)
    mesh = mesh_tet4(part, graded_size(part, wall_size=0.5, growth_rate=1.2, max_size=4.0))      # 0.5 mm at the surface, growing inwards
    mesh = mesh_tet4(air, 2.0, boundary_layers=inflation_layers(body, first_height=0.05, growth_rate=1.2, layers=6),
                       zones={'inlet': inlet, 'outlet': outlet})                # a flow: prism layers on the wall, named patches

    # Surface
    surface = mesh_surface_tri(part, 1.0)                # the surface alone, as triangles
    surface = mesh_surface_quad(part, 1.0)           # the surface alone, as quadrilaterals

    print(mesh)                                      # what it has
    print(mesh.quality().report())                   # skewness, orthogonal quality, aspect ratio ... and what they mean for a solver
    mesh.export('part.msh')                          # Gmsh;  .vtu .inp .bdf .unv .stl ...;  format='fluent' | 'openfoam'

The tetrahedra are made by isosurface stuffing on the body's own field (a conforming tetrahedral mesh whose boundary vertices lie on the surface: see the kernel's
tetmesh.hpp), and what is asked of them afterwards is done to that mesh.  Arguments that the functions have in common:

    element_size    a number (mm), or -- for the tetrahedra -- a field: a body whose value at a point is the element size there, a function of a list of points that returns their sizes,
                    or graded_size(...).  The mesh starts at the largest size the field has and is refined where it asks for less: the longest edge of a tetrahedron
                    is bisected (with the tetrahedra round it, so the mesh stays conforming) until its edges are no longer than the size asked for there, and the new
                    vertices of the boundary are put on the surface.  Without one: about forty elements along the longest side, no more than about a hundred thousand tetrahedra
                    (about fifteen thousand cells for the hexahedra)
    boundary_layers inflation_layers(...): layers on the surface of a solid, growing into the fluid (the first height, the growth rate, the number of layers): prisms with mesh_tet4,
                    hexahedra with mesh_hex_extrusion
    zones           the names of the patches of the boundary: {'inlet': region, 'outlet': region}: a boundary face lies in the first region that holds its middle,
                    and the rest are `default_zone` ('wall'); feature_angle splits that one into walls at the creases
    cell_zones      the same for the cells: {'solid': region}; the rest are 'fluid'
    optimize        the poor tetrahedra are mended after the meshing (on by default): the nodes of poor tetrahedra are moved (those on the surface along it, `smooth`
                    rounds in each pass), edges and faces swapped and interior nodes collapsed, wherever that makes the worst tetrahedron round them better

Every length is in mm, as everywhere in FielDes.  A mesh is a description of the elements: it has no material, no conditions and no solver settings.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import math
import time
from array import array

from fieldes.ffi import lib, libfive_region_t
from fieldes.shape import Shape
from fieldes.stdlib.element_size import default_element_size

__all__ = ['mesh_tet4', 'mesh_tet10', 'mesh_hex_quanta', 'mesh_hex20_quanta', 'mesh_cartesian', 'mesh_hex_extrusion', 'mesh_surface_tri', 'mesh_surface_quad', 'graded_size', 'curvature_size', 'inflation_layers', 'MeshError']

# What the kinds of element are: the nodes they have, the nodes at their corners, and their faces -- each with the nodes in an order that has its normal (the
# right-hand rule) outward from the element.  The corners are numbered so that a tetrahedron has det(p1 - p0, p2 - p0, p3 - p0) > 0, a hexahedron has the
# nodes 0 1 2 3 round its bottom (seen from the top: counter-clockwise) and 4 5 6 7 above them, a prism has 0 1 2 round its bottom (counter-clockwise seen from
# the top) and 3 4 5 above them.  The ten nodes of a tetrahedron are its corners and the middle of the edges 0-1, 1-2, 2-0, 0-3, 1-3, 2-3
NODES = {'tri3': 3, 'quad4': 4, 'tet4': 4, 'tet10': 10, 'hex8': 8, 'hex20': 20, 'prism6': 6, 'pyra5': 5}
CORNERS = {'tri3': 3, 'quad4': 4, 'tet4': 4, 'tet10': 4, 'hex8': 8, 'hex20': 8, 'prism6': 6, 'pyra5': 5}
SOLIDS = ('tet4', 'tet10', 'pyra5', 'prism6', 'hex8', 'hex20')
# A pyramid has 0 1 2 3 round its base (counter-clockwise seen from the apex: the right-hand rule has the normal toward it) and the apex 4.  A polyhedron ('poly', the cells that a surface cuts out of a
# cube of a Cartesian grid, see bodyfit.py) has any number of faces of any number of nodes: mesh.cells['poly'] is the nodes of all its faces one after the other, mesh.poly_sizes the nodes of each face
# and mesh.poly_faces_of the faces of each cell; the same for a boundary face of more than four nodes, mesh.faces['poly'] and mesh.face_sizes['poly']
POLY = 'poly'
SURFACES = ('tri3', 'quad4')
FACES = {
    'tet4': ((0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)),
    'hex8': ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)),
    'prism6': ((0, 2, 1), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)),
    'pyra5': ((0, 3, 2, 1), (0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)),
}
FACES['tet10'] = FACES['tet4']
FACES['hex20'] = FACES['hex8']
EDGES = {
    'tet4': ((0, 1), (1, 2), (2, 0), (0, 3), (1, 3), (2, 3)),
    'hex8': ((0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7)),
    'prism6': ((0, 1), (1, 2), (2, 0), (3, 4), (4, 5), (5, 3), (0, 3), (1, 4), (2, 5)),
    'pyra5': ((0, 1), (1, 2), (2, 3), (3, 0), (0, 4), (1, 4), (2, 4), (3, 4)),
}
EDGES['tet10'] = EDGES['tet4']
EDGES['hex20'] = EDGES['hex8']
# (the kinds of element and ways of meshing the one mesher behind the functions below tells apart: 'tet' and 'tet10' the tetrahedra; 'hex_quanta' the whole cubes of a Cartesian grid, Salome's Body Fitting with Quanta, and
# 'hex_quanta20' the same with a node in the middle of every edge; 'cartesian' the grid with the cubes the surface cuts made as the cells that are inside them; 'hex_extrusion' a prism swept from a face of quadrilaterals,
# Salome's Extrusion 3D.  Each has a function of its own, with only its arguments: mesh_tet4, mesh_tet10, mesh_hex_quanta, mesh_hex20_quanta, mesh_cartesian, mesh_hex_extrusion)
ELEMENTS = ('tet', 'tet10', 'hex_quanta', 'hex_quanta20', 'cartesian', 'hex_extrusion')
_PAIRS = ((0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3))
DEFAULT_HEXAHEDRA = 15000           # the budget of hexahedra of the default element size (they are made in python: about 5 ms each)
CARTESIAN_DEFAULT_FACTOR = 0.66     # the edge of the cube of mesh_cartesian at its default size over the edge of the tetrahedra's: skin faces of the same area (a triangle of edge h has 0.43 h^2, a face of a cube h^2)
CARTESIAN_DEFAULT_CELLS = 40000     # the budget of cells of mesh_cartesian at its default size
DEFAULT_ELEMENTS = 100000           # the budget of tetrahedra of the default element size: a plain cube takes about 20 s and its elements are drawn (the viewer draws up to MAX_DISPLAYED)


class MeshError(RuntimeError):
    ''' A mesh that cannot be made, and why '''


def _need_kernel():
    if getattr(lib, 'libfive_tetmesh_new', None) is None:
        raise MeshError('this FielDes library is too old for meshing')


def _edge(a, b):
    return (a, b) if a < b else (b, a)


def _eval(field, points):
    from fieldes.stdlib.fields import _eval_many
    return _eval_many(field, points)


def _det6(P, a, b, c, d):
    ''' Six times the volume of the tetrahedron a b c d (positive when it is right-handed) '''
    ax, ay, az = P[3 * a], P[3 * a + 1], P[3 * a + 2]
    ux, uy, uz = P[3 * b] - ax, P[3 * b + 1] - ay, P[3 * b + 2] - az
    vx, vy, vz = P[3 * c] - ax, P[3 * c + 1] - ay, P[3 * c + 2] - az
    wx, wy, wz = P[3 * d] - ax, P[3 * d + 1] - ay, P[3 * d + 2] - az
    return ux * (vy * wz - vz * wy) - uy * (vx * wz - vz * wx) + uz * (vx * wy - vy * wx)


################################################################################
# Element sizes

class _Sizer(object):
    ''' The element size at any points: a number, a field (a body whose value at a point is the size), or a function of a list of points '''

    def __init__(self, size):
        self.uniform = None
        self.field = None
        self.function = None
        if isinstance(size, Shape):
            self.field = size
        elif callable(size):
            self.function = size
        else:
            try:
                self.uniform = float(size)
            except (TypeError, ValueError):
                raise TypeError('element_size is a number (mm), a field or a function of a list of points, not {!r}'.format(size)) from None
            if not self.uniform > 0:
                raise MeshError('element_size must be positive')

    def __call__(self, points):
        if self.uniform is not None:
            return [self.uniform] * len(points)
        values = _eval(self.field, points) if self.field is not None else list(self.function(points))
        return [max(1e-9, float(v)) for v in values]

    def coarsest(self, body, lo, hi):
        ''' The largest size the field asks for inside the body (a number is its own): where the mesh starts '''
        if self.uniform is not None:
            return self.uniform
        n = 12
        points = []
        for k in range(n):
            for j in range(n):
                for i in range(n):
                    points.append((lo[0] + (i + 0.5) * (hi[0] - lo[0]) / n, lo[1] + (j + 0.5) * (hi[1] - lo[1]) / n, lo[2] + (k + 0.5) * (hi[2] - lo[2]) / n))
        inside = [p for p, v in zip(points, _eval(body, points)) if v <= 0.0] or points
        return max(self(inside))


class _Graded(object):
    ''' graded_size(...): the size grows with the distance from a surface '''

    def __init__(self, wall, wall_size, growth_rate, max_size):
        self.wall = wall
        self.wall_size = float(wall_size)
        self.growth = float(growth_rate)
        self.max_size = None if max_size is None else float(max_size)
        if not self.wall_size > 0:
            raise MeshError('graded_size: wall_size must be positive')
        if not self.growth >= 1.0:
            raise MeshError('graded_size: growth_rate is 1 or more (1.2: the size grows by 0.2 mm for every mm from the surface)')
        self._distance = None

    def __call__(self, points):
        if self._distance is None:
            from fieldes.stdlib.fields import exact_distance, _shape_bounds
            lo, hi = _shape_bounds(self.wall, None)
            size = max(hi[i] - lo[i] for i in range(3))
            reach = size if self.max_size is None or self.growth <= 1.0 else min(size, (self.max_size - self.wall_size) / (self.growth - 1.0) + self.wall_size)
            self._distance = exact_distance(self.wall, reach=max(reach, 4.0 * self.wall_size))
        out = []
        for d in _eval(self._distance, points):
            s = self.wall_size + (self.growth - 1.0) * abs(d)
            out.append(s if self.max_size is None else min(s, self.max_size))
        return out


def graded_size(wall, wall_size, growth_rate=1.2, max_size=None):
    ''' An element size that grows with the distance from a surface: `wall_size` at the surface of `wall`, and `growth_rate - 1` more for every mm from it (1.2: the
        size has grown by 0.2 mm after one mm), up to `max_size`.  It is what mesh_tet4's element_size takes for a mesh that is fine where the flow or the stress is
        and coarse elsewhere:

            mesh = mesh_tet4(air, graded_size(body, wall_size=0.5, growth_rate=1.2, max_size=8))

        `wall` is a body: the distance is that from the surface of it, on either side (a solid in a fluid, or the fluid's own boundary) '''
    return _Graded(wall, wall_size, growth_rate, max_size)


class _Curvature(object):
    ''' curvature_size(...): the size is the one asked where the surface is flat and smaller where it curves.  The surface of the body is sampled once, with the kernel's own coarse mesh of it; at every sample
        the largest curvature is the largest, over three directions of the tangent plane, of the angle between the surface's normals at the two ends of a short step along it over the length of the
        step (a sphere of radius R gives 1 / R, a cylinder 1 / R across it and 0 along it); a turn of more than half a radian over the step is an edge, and an edge is not a curve: edges are found by the
        mesher's own recognition of features.  The size at a sample is the circumference of the curve it has over the number of `segments` asked for, 2 pi R / segments, between `min_size` and the size asked; and away from the
        samples it grows with the distance, `growth_rate - 1` mm for every mm, up to the size asked '''

    def __init__(self, body, element_size, segments, min_size, growth_rate):
        if not isinstance(body, Shape):
            raise TypeError('curvature_size: the body is a Shape, not {!r}'.format(type(body).__name__))
        self.body = body
        self.size = float(element_size)
        self.segments = float(segments)
        self.min_size = float(min_size) if min_size is not None else 0.25 * self.size
        self.growth = float(growth_rate)
        if not self.size > 0:
            raise MeshError('curvature_size: element_size must be positive')
        if not self.segments >= 4:
            raise MeshError('curvature_size: segments is 4 or more (the elements round a full circle: 12 is the common choice)')
        if not 0 < self.min_size <= self.size:
            raise MeshError('curvature_size: min_size is positive and not more than element_size')
        if not self.growth > 1.0:
            raise MeshError('curvature_size: growth_rate is more than 1 (1.3: the size grows by 0.3 mm for every mm from a curve)')
        self._cells = None
        self._cell = None
        self._reach = 0
        self._typical = 1.0

    def _measure(self, points):
        ''' [(the point put on the surface, its tangent directions, the size its curvature asks for)] of the points whose surface is a curve -- not an edge, not flat.  The curvature in a direction is the angle
            between the surface's normals at the two ends of a short step along the surface over the length of the step (a circle of radius R: 2 t / R over 2 t); where that angle is more than half a
            radian the surface has an edge there, and an edge is not a curve.  (A second difference of the field would do for a smooth field, but the field of a body made of other bodies is a distance to a
            mesh, with a kink at every corner of its facets.) '''
        step = 0.08 * self.size
        scales = [self.size] * len(points)
        points = [tuple(q) for q in _snap_to_surface(self.body, points, scales)]
        grads = _field_gradients(self.body, points, scales)
        samples = []
        for q, (_, g) in zip(points, grads):
            gl = math.sqrt(_dot3(g, g))
            if gl < 0.9 * self._typical or gl <= 1e-12:
                continue                      # (an edge or a corner: not a curve)
            n = (g[0] / gl, g[1] / gl, g[2] / gl)
            a = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
            t1 = _cross3(n, a)
            l1 = math.sqrt(_dot3(t1, t1))
            t1 = (t1[0] / l1, t1[1] / l1, t1[2] / l1)
            t2 = _cross3(n, t1)
            dirs = [tuple(math.cos(math.radians(60 * k)) * t1[c] + math.sin(math.radians(60 * k)) * t2[c] for c in range(3)) for k in range(3)]
            samples.append((q, dirs))
        ends = []
        for q, dirs in samples:
            for d in dirs:
                ends.append((q[0] + step * d[0], q[1] + step * d[1], q[2] + step * d[2]))
                ends.append((q[0] - step * d[0], q[1] - step * d[1], q[2] - step * d[2]))
        normals = _field_gradients(self.body, ends, [self.size] * len(ends))
        out = []
        for i, (q, dirs) in enumerate(samples):
            best = 0.0
            edge = False
            for k in range(3):
                (_, ga), (_, gb) = normals[6 * i + 2 * k], normals[6 * i + 2 * k + 1]
                la, lb = math.sqrt(_dot3(ga, ga)), math.sqrt(_dot3(gb, gb))
                if la <= 1e-12 or lb <= 1e-12:
                    edge = True
                    break
                c = max(-1.0, min(1.0, _dot3(ga, gb) / (la * lb)))
                angle = math.acos(c)
                if angle > 0.5:
                    edge = True
                    break
                best = max(best, angle / (2.0 * step))
            if edge or best <= 1e-9:
                continue
            h = min(self.size, max(self.min_size, 2.0 * math.pi / (self.segments * best)))
            if h < self.size:
                out.append((q, dirs, h))
        return out

    def _build(self):
        from fieldes.stdlib.fields import _shape_bounds
        lo, hi = _shape_bounds(self.body, None)
        P, T = _kernel_tets(self.body, lo, hi, self.size)
        tets = [tuple(T[4 * i:4 * i + 4]) for i in range(len(T) // 4)]
        nodes = sorted(_boundary_nodes(tets))
        first = [_point(P, v) for v in nodes]
        strengths = sorted(math.sqrt(_dot3(g, g)) for _, g in _field_gradients(self.body, first, [self.size] * len(first)))
        self._typical = strengths[len(strengths) // 2] if strengths else 1.0
        # (the coarse mesh has few nodes on a small curve: the curved samples walk along the surface -- a step of 0.7 of the size they ask for, in the six tangent directions -- until the curve is covered)
        found = []
        near = {}
        cell = 0.5 * self.min_size

        def is_near(q, radius):
            kx, ky, kz = int(math.floor(q[0] / cell)), int(math.floor(q[1] / cell)), int(math.floor(q[2] / cell))
            reach = int(math.ceil(radius / cell))
            for dx in range(-reach, reach + 1):
                for dy in range(-reach, reach + 1):
                    for dz in range(-reach, reach + 1):
                        for x, y, z in near.get((kx + dx, ky + dy, kz + dz), ()):
                            if (q[0] - x) ** 2 + (q[1] - y) ** 2 + (q[2] - z) ** 2 < radius * radius:
                                return True
            return False

        def add(q, dirs, h):
            found.append((q, dirs, h))
            near.setdefault((int(math.floor(q[0] / cell)), int(math.floor(q[1] / cell)), int(math.floor(q[2] / cell))), []).append(q)

        frontier = []
        for q, dirs, h in self._measure(first):
            if not is_near(q, 0.5 * h):
                add(q, dirs, h)
                frontier.append((q, dirs, h))
        for _ in range(80):
            if not frontier or len(found) > 40000:
                break
            candidates = []
            for q, dirs, h in frontier:
                reach = 0.7 * h
                for d in dirs:
                    for sign in (1.0, -1.0):
                        c = (q[0] + sign * reach * d[0], q[1] + sign * reach * d[1], q[2] + sign * reach * d[2])
                        if not is_near(c, 0.5 * reach):
                            candidates.append(c)
            frontier = []
            for q, dirs, h in self._measure(candidates):
                if not is_near(q, 0.5 * 0.7 * h):
                    add(q, dirs, h)
                    frontier.append((q, dirs, h))
        cells = {}
        big = 1.5 * self.size
        for q, dirs, h in found:
            cells.setdefault((int(math.floor(q[0] / big)), int(math.floor(q[1] / big)), int(math.floor(q[2] / big))), []).append((q[0], q[1], q[2], h))
        self._cells = cells
        self._cell = big
        self._reach = max(1, min(3, int(math.ceil((self.size / (self.growth - 1.0)) / big))))

    def __call__(self, points):
        if self._cells is None:
            self._build()
        cells, cell, reach, size, g = self._cells, self._cell, self._reach, self.size, self.growth - 1.0
        out = []
        for p in points:
            best = size
            if cells:
                kx, ky, kz = int(math.floor(p[0] / cell)), int(math.floor(p[1] / cell)), int(math.floor(p[2] / cell))
                for dx in range(-reach, reach + 1):
                    for dy in range(-reach, reach + 1):
                        for dz in range(-reach, reach + 1):
                            found = cells.get((kx + dx, ky + dy, kz + dz))
                            if found:
                                for x, y, z, h in found:
                                    d = math.sqrt((p[0] - x) ** 2 + (p[1] - y) ** 2 + (p[2] - z) ** 2)
                                    v = h + g * d
                                    if v < best:
                                        best = v
            out.append(best)
        return out

    def coarsest(self, body, lo, hi):
        return self.size


def curvature_size(body, element_size, segments=12, min_size=None, growth_rate=1.3):
    ''' An element size that follows the curvature of a body: `element_size` where its surface is flat, and smaller where it curves, so that a hole, a fillet or a small sphere has `segments` elements round
        a full circle (12: a chord is 3.4 % of the radius off the circle) -- the size at a curve of radius R is 2 pi R / segments, not below `min_size` (default a quarter of `element_size`) -- growing back
        to `element_size` by `growth_rate - 1` mm for every mm from the curve.  It is what mesh_tet4's element_size takes, like graded_size:

            mesh = mesh_tet4(part, curvature_size(part, element_size=3.0, segments=16))

        Edges and corners are not curves: they are found by the mesher itself.  `body` is the body that is meshed '''
    return _Curvature(body, element_size, segments, min_size, growth_rate)


################################################################################
# Thin parts: the element size that holds them (H193)

THIN_AREA = 0.10            # the part of the surface that may lie in walls too thin for the elements before the size is made for them (a rib or two is left to the size the body would have had)
THIN_AT_RISK = 1.4          # a wall thinner than this many spacings is lost or half filled (measured: a plate held all of its volume from 1.0 spacings, the curved wall of a tube from 1.25; below 1.0 it held 41 %)
THIN_COARSEST = 1.5         # the coarsest a lattice may be, where the budget asks for it, for the walls that count: the wall over this (above THIN_AT_RISK, so that it is not taken for thin again)
THIN_ACROSS = 2.0           # elements across the walls that count, where the budget allows
THIN_CELLS = 60e6           # the most cubes of a lattice that the probe looks at (the kernel's own limit is 80 million)
THIN_FEW_ACROSS = 5.0       # a linear tetrahedron bends a wall well from about this many elements across it (measured: two across, a steel cantilever plate 19 to 22 % too stiff)
# (the sentence of the advice, for the note of the mesh's size and for the analyses)
LOCKING_ADVICE = ('linear tetrahedra with so few across a wall are too stiff in bending (a cantilever plate came out about 20 % too stiff, and its frequencies too high): '
                  'for the deflection, the stress and the frequencies of a thin part use the 10-node tetrahedron (mesh_tet10 for a mesh, element=\'tet10\' for an analysis)')


def locking_note(wall, h):
    ''' The advice about linear tetrahedra for a body whose walls (the thickness that a tenth of its surface is thinner than) are `wall` mm and whose elements are `h` mm: a sentence when fewer than THIN_FEW_ACROSS
        elements lie across them, else None '''
    if not (wall and h and wall < THIN_FEW_ACROSS * h):
        return None
    return 'the body has walls about {:.3g} mm thick, {:.2g} elements of {:.3g} mm across them: {}'.format(wall, wall / h, h, LOCKING_ADVICE)


def _probe_surface(shape, lo, hi, h, reach=0.6, count=3000):
    ''' The kernel's look at the lattice that a mesh of the box (lo, hi) at the spacing h is cut from (kernel/src/fea/tetmesh.cpp probeSurface): (the spacing the lattice was fitted to, its vertices, the ones inside the
        shape, the ones within `reach` of the spacing of the surface, every how many of those the samples are, the samples [(x, y, z, the field's value)]) '''
    _need_kernel()
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i], hi[i]
    ptr = lib.libfive_tetmesh_probe(shape.ptr, region, float(h), float(reach), int(count))
    if not ptr:
        message = lib.libfive_tetmesh_last_message()
        raise MeshError('the body cannot be looked at' + (': ' + message.decode('utf-8', 'replace') if message else ''))
    try:
        counts = (ctypes.c_int64 * 5)()
        lib.libfive_probe_counts(ptr, counts)
        n = int(counts[3])
        buf = (ctypes.c_double * max(1, 4 * n))()
        lib.libfive_probe_samples(ptr, buf)
        spacing = float(lib.libfive_probe_spacing(ptr))
    finally:
        lib.libfive_probe_delete(ptr)
    samples = [(buf[4 * i], buf[4 * i + 1], buf[4 * i + 2], buf[4 * i + 3]) for i in range(n)]
    return spacing, int(counts[0]), int(counts[1]), int(counts[2]), max(1, int(counts[4])), samples


def _thickness(shape, points, h):
    ''' How thick the material is at each point that lies near the surface: the point is put on the nearest surface, and the field is read along the normal into the body, in steps of a sixteenth of h, to where
        it leaves it again (the zero between two reads found by a straight line).  A wall thinner than a step is half a step (the reads are beyond it); one that is not left within two element sizes is
        two (a wall that thick holds nodes of the lattice).  None where the field has no gradient or the point could not be put on the surface '''
    if not points:
        return []
    step = h / 16.0
    reads = 32
    scales = [h] * len(points)
    snapped = [tuple(q) for q in _snap_to_surface(shape, points, scales)]
    rays = []
    for q, (f, g) in zip(snapped, _field_gradients(shape, snapped, scales)):
        length = math.sqrt(_dot3(g, g))
        # (a point that could not be put on the surface -- the field of a cutter that reaches out of the body is the distance to the cutter's face over the open side of a shell, a ridge that the steps
        # swing across -- says nothing about a wall: it is left out)
        rays.append(None if length <= 1e-12 or abs(f) > 0.05 * h else (q, (-g[0] / length, -g[1] / length, -g[2] / length)))
    pts = []
    for ray in rays:
        if ray is not None:
            q, n = ray
            for k in range(1, reads + 1):
                s = k * step
                pts.append((q[0] + s * n[0], q[1] + s * n[1], q[2] + s * n[2]))
    values = _eval(shape, pts)
    out = []
    at = 0
    for ray in rays:
        if ray is None:
            out.append(None)
            continue
        v = values[at:at + reads]
        at += reads
        found = None
        seen = False
        for k in range(reads):
            f = v[k]
            if f < 0.0:
                seen = True
            elif seen:
                a = v[k - 1] if k >= 1 else 0.0
                found = (k + (-a / (f - a) if f != a else 1.0)) * step
                break
        out.append(found if found is not None else (0.5 * step if not seen else reads * step))
    return out


def thin_size(shape, lo, hi, h, budget, tets_per_cube=12.0, hard=4000000, across=None, walls=None, unit='tetrahedra'):
    ''' The element size that holds the thin walls of a body (H193, the user, 2026-11-13: "make it recoginze features properly"; found 2026-11-14: a shell box of walls 1.2 mm was refused at its default size, a 1.5 mm
        plate kept 48 % of its volume, a tube with a wall of 1 mm 1 %).  The tetrahedra are cut from a lattice of spacing h, and a wall that is not about as thick as the spacing is lost or half filled: measured, a plate
        of 1.5 mm held 41 % of its volume at spacings of 2.5 and 1.9 mm and all of it from 1.5, the curved wall of a tube 58 % at one spacing across and all of it from 1.25.  The kernel gives a sample of the lattice
        vertices that lie near the surface (probeSurface); the thickness of the material is measured there, along the normal into the body, and when THIN_AREA of them or more lie in walls thinner than THIN_AT_RISK
        spacings the size is made THIN_ACROSS elements across the wall that a tenth of the surface is thinner than (the thinner walls beyond it are left to chance, as a rib is).  The size is then looked at again at that
        size (a thinner wall may be found), and when the tetrahedra there are more than `budget` it is made coarser again as far as the budget asks, never coarser than the walls over THIN_COARSEST: the budget
        gives way to the walls, up to `hard` tetrahedra.  Netgen does the same by restricting the local size by the distance across a thin part (stlgeom RestrictHChartDistOneChart).
        `unit` names what is counted in the note ('hexahedra' for a grid of cubes: `tets_per_cube` is then the 2 vertices that a cubic lattice has to the cube, and the count the cells).
        Returns (the size, a note on what was found or None) '''
    size = [float(hi[a]) - float(lo[a]) for a in range(3)]
    box = size[0] * size[1] * size[2]
    h0 = h
    wall = None
    tets = 0.0
    probed = h
    for _ in range(5):
        if box / h ** 3 > THIN_CELLS:
            break
        try:
            spacing, vertices, inside, near, stride, samples = _probe_surface(shape, lo, hi, h)
        except MeshError:
            break
        probed = h
        tets = 0.5 * tets_per_cube * inside
        thick = sorted(t for t in _thickness(shape, [(s[0], s[1], s[2]) for s in samples], spacing) if t is not None) if samples else []
        if len(thick) < 30:
            break
        if walls is not None and 'wall' not in walls:
            walls['wall'] = thick[int(THIN_AREA * len(thick))]          # (the walls a tenth of the surface is thinner than, for the advice on linear tetrahedra: locking_note)
        if sum(1 for t in thick if t < THIN_AT_RISK * spacing) >= THIN_AREA * len(thick):
            now = thick[int(THIN_AREA * len(thick))]
            wall = now if wall is None else min(wall, now)
            new = max(wall / (across or THIN_ACROSS), 1.01 * (box / THIN_CELLS) ** (1.0 / 3.0))
            if new >= 0.97 * h:
                break
            h = new
            continue
        break
    if wall is None or h >= h0:
        return h0, None
    if tets > budget:
        h = max(h, min(probed * (tets / float(budget)) ** (1.0 / 3.0), wall / THIN_COARSEST))
    if tets * (probed / h) ** 3 > hard:
        h = probed * (tets / float(hard)) ** (1.0 / 3.0)
    if h >= 0.97 * h0:
        return h0, None
    count = tets * (probed / h) ** 3
    note = ('the body has walls {:.3g} mm thick, which a {} of {:.3g} mm loses or half fills: the element size is {:.3g} mm, {:.2g} elements across them, about {:,} {}'.format(
        wall, 'lattice' if unit == 'tetrahedra' else 'grid', h0, h, wall / h, int(round(count, -3)), unit))
    if wall / h < (across or THIN_ACROSS) * 0.97:
        note += ' (a finer size is held back by the budget of {:,} {}, which gives way to the walls only as far as it takes to hold them)'.format(int(budget if count <= hard else hard), unit)
    if not across and unit == 'tetrahedra':
        # (measured: a steel cantilever plate, two linear tetrahedra across, came out 19 % too stiff; the ten-node element, 4 %)
        note += '; ' + LOCKING_ADVICE
    return h, note


class _Inflation(object):
    def __init__(self, on, first_height, growth_rate, layers):
        self.on = on
        self.first = float(first_height)
        self.growth = float(growth_rate)
        self.layers = int(layers)
        if not self.first > 0:
            raise MeshError('inflation_layers: first_height must be positive')
        if not self.growth >= 1.0:
            raise MeshError('inflation_layers: growth_rate is 1 or more')
        if self.layers < 1:
            raise MeshError('inflation_layers: at least one layer')

    def heights(self):
        return [self.first * self.growth ** i for i in range(self.layers)]

    def thickness(self):
        return sum(self.heights())


def inflation_layers(on, first_height, growth_rate=1.2, layers=5):
    ''' Layers of prisms (mesh_tet4) or of hexahedra (mesh_hex_extrusion) on the surface of a solid, for a flow: `layers` of them, the one at the wall `first_height` thick and each one
        `growth_rate` times as thick as the one before it (the first height is what sets y+: about y+ times the viscosity over the friction velocity).  mesh_tet4's `boundary_layers` takes it:

            air = difference(box_of_air, body)
            mesh = mesh_tet4(air, 2.0, boundary_layers=inflation_layers(body, first_height=0.05, growth_rate=1.2, layers=6))

        `on` is the solid whose surface the layers grow from, into the mesh.  The core of the mesh is the body with that much taken off the surface, the layers are
        what is between it and the surface: they are made of its boundary, moved to the surface in steps, so the layers follow it -- and at a sharp convex corner,
        where the direction of the surface is not defined, they are skewed: round the corners of a body that needs good layers.  The total thickness must be less than
        half of the thinnest passage of the fluid '''
    return _Inflation(on, first_height, growth_rate, layers)


################################################################################
# The mesh

class MeshQuality(object):
    ''' How good the elements are, in the numbers a solver is told about: skewness (0 is a regular element, 1 degenerate), orthogonal quality (1 is best, 0 worst),
        the ratio of the longest edge to the shortest, the volume of an element beside the ones it touches, and the smallest dihedral angle of the tetrahedra.
        `metrics[name]` is (minimum, mean, maximum); report() says what they mean '''

    def __init__(self):
        self.counts = {}
        self.volume = 0.0
        self.negative = 0
        self.metrics = {}
        self.notes = []
        self.poor_jacobian = 0          # (the cells whose scaled Jacobian is below 0.2: where the elements are bent or flattened)
        self.skewed = None              # (the cells above skewness 0.85, the cells above 0.95, the cells measured)
        self.graded = False             # (the mesh was made with a size that varies on purpose: graded_size or a field)
        self.spread = None              # (how much the size of the cells varies, H202: the volume of the largest over the smallest, and the 5th and the 95th percentile of the volumes over the median)
        self.sampled = 0                # (every this-many-th cell was measured: 0 for all of them)
        self.seconds = 0.0
        self.per_cell = None            # (the value of every cell, when it was asked for: see _measure)

    def __getitem__(self, name):
        return self.metrics[name]

    def report(self):
        lines = ['Mesh quality: ' + ', '.join('{:,} {}'.format(n, k) for k, n in self.counts.items() if n)
                 + '   (volume {:.6g} mm^3, measured in {:.1f} s{})'.format(self.volume, self.seconds, '' if not self.sampled else ', every {}th cell'.format(self.sampled))]
        if self.negative:
            lines.append('  !! {:,} elements have no volume or are inside out: a solver will refuse the mesh'.format(self.negative))
        shown = (('scaled_jacobian', 'scaled Jacobian (1 regular .. 0 flat, below 0 inside out)', _jacobian_word, 'min'),
                 ('skewness', 'skewness (0 regular .. 1 degenerate)', _skew_word, 'max'),
                 ('orthogonal_quality', 'orthogonal quality (1 best .. 0 worst)', _orth_word, 'min'),
                 ('aspect_ratio', 'aspect ratio (longest edge / shortest)', _aspect_word, 'max'),
                 ('volume_ratio', 'volume ratio (an element / the ones it touches)', _ratio_word, 'max'),
                 ('min_dihedral', 'smallest dihedral angle of a tetrahedron (degrees)', _dihedral_word, 'min'))
        for key, label, word, worst in shown:
            if key not in self.metrics:
                continue
            lo, mean, hi = self.metrics[key]
            value = hi if worst == 'max' else lo
            lines.append('  {:<56} min {:<10.4g} mean {:<10.4g} max {:<10.4g}  {}'.format(label, lo, mean, hi, word(value)))
            if key == 'scaled_jacobian' and self.poor_jacobian:
                lines.append('      {:,} cells have a scaled Jacobian below 0.2'.format(self.poor_jacobian))
            if key == 'skewness' and self.skewed and self.skewed[0]:
                over85, over95, measured = self.skewed
                lines.append('      {:,} of the {:,} cells ({:.2g} %) are above 0.85, the most that CFD solvers like, and {:,} above 0.95'.format(over85, measured, 100.0 * over85 / measured, over95))
        if self.spread is not None:
            largest, low, high = self.spread
            lines.append('  {:<56} largest / smallest {:<8.4g} 5 % .. 95 % of the median {:.2f} .. {:.2f}  {}'.format(
                'size of the elements (volume)', largest, low, high, 'graded on purpose: the size was asked to vary' if self.graded else _spread_word(largest, low, high)))
        lines.extend('  ' + n for n in self.notes)
        return '\n'.join(lines)

    def __repr__(self):
        return self.report()


def _jacobian_word(v):
    return 'excellent' if v > 0.8 else 'good' if v > 0.5 else 'fair' if v > 0.3 else 'poor: bent elements, a solver loses accuracy in them' if v > 0.0 else 'inside out: a solver will refuse the mesh'


def _spread_word(largest, low=None, high=None):
    ''' How even the sizes are, in a word.  By the 5 % .. 95 % of the volumes over the median where they are known: a few slivers at the surface make the largest over the smallest 18 on a plate with a hole made at ONE size
        (2026-11-14: the report of the example of the docs said "uneven" of it) while 95 % of its cells lie within a factor 2 of each other; a mesh of which a tenth is far smaller than the rest has a wide 5 % .. 95 %
        and is called so.  The largest over the smallest decides only where the percentiles are not known '''
    v = high / low if low and high and low > 0.0 else largest
    return ('even' if v < 2.5 else 'fair: a result is the more credible the more even the elements are' if v < 6.0
            else 'uneven: the elements differ so much in size that the result depends on where the mesh happens to fall')


def _skew_word(v):
    return 'excellent' if v < 0.25 else 'good' if v < 0.5 else 'fair' if v < 0.8 else 'poor' if v < 0.95 else 'bad: most solvers want 0.95 or less, CFD 0.85'


def _orth_word(v):
    return 'excellent' if v > 0.95 else 'very good' if v > 0.7 else 'good' if v > 0.2 else 'acceptable' if v > 0.15 else 'poor: CFD solvers want more than 0.1'


def _aspect_word(v):
    return 'regular' if v < 5 else 'moderate' if v < 10 else 'stretched (the cells of a boundary layer are, on purpose)' if v < 1000 else 'extreme'


def _ratio_word(v):
    return 'smooth' if v < 2.5 else 'acceptable' if v < 5 else 'abrupt: a solver wants 5 or less' if v < 20 else 'very abrupt'


def _dihedral_word(v):
    return 'good' if v > 20 else 'fair' if v > 10 else 'poor: slivers'


class Mesh(object):
    ''' A mesh: nodes, the cells (solids: tetrahedra, 10-node tetrahedra, hexahedra, prisms) and the faces of its boundary (triangles, quadrilaterals), each of
        them in a zone with a name.  Everything is in flat arrays:

            nodes                   array('d'): x y z of every node
            cells[kind]             array('i'): the nodes of every cell of that kind, in the order the kind has them (see meshing.py)  -- 'tet4' 'tet10' 'hex8' 'prism6'
            faces[kind]             the boundary: 'tri3' 'quad4', the corner nodes in order, normal out
            cell_zone[kind], face_zone[kind]   array('i'): the zone of every cell / face (an index into cell_zone_names / zone_names)

        quality() measures it, export(path) writes it for another program '''

    def __init__(self, name='mesh'):
        self.name = name
        self.nodes = array('d')
        self.cells = {}
        self.poly_sizes = array('i')    # (polyhedra, see POLY: the nodes of each face, and the faces of each cell)
        self.poly_faces_of = array('i')
        self.cell_zone = {}
        self.cell_zone_names = ['fluid']
        self.faces = {}
        self.face_sizes = {}            # (the boundary faces of more than four nodes: mesh.faces['poly'] and the nodes of each)
        self.face_zone = {}
        self.zone_names = []
        self.element = 'tet'
        self.element_size = None
        self.notes = []
        self.seconds = 0.0
        self.body = None                # (the body it is the mesh of: what FielDes draws it on)
        self.box = None                 # (the box mesh_tet4 meshed the body in: what the volume of the mesh is measured against, see _volume_note)
        self._shown = None

    # --- counts
    @property
    def node_count(self):
        return len(self.nodes) // 3

    def cell_count(self, kind=None):
        if kind is not None:
            if kind == POLY:
                return len(self.poly_faces_of) if self.cells.get(POLY) else 0
            return len(self.cells.get(kind, ())) // NODES[kind]
        return sum(len(self.poly_faces_of) if k == POLY else len(c) // NODES[k] for k, c in self.cells.items())

    def face_count(self, kind=None, zone=None):
        total = 0
        for k, conn in self.faces.items():
            if kind is not None and k != kind:
                continue
            n = len(self.face_sizes.get(POLY, ())) if k == POLY else len(conn) // NODES[k]
            if zone is not None:
                z = self.zone_names.index(zone) if isinstance(zone, str) else zone
                total += sum(1 for v in self.face_zone[k] if v == z)
            else:
                total += n
        return total

    def polyhedra(self):
        ''' The polyhedra: for each, its faces as tuples of nodes, turned outward '''
        conn = self.cells.get(POLY)
        if not conn:
            return
        pos = 0
        s = 0
        for count in self.poly_faces_of:
            faces = []
            for _ in range(count):
                n = self.poly_sizes[s]
                s += 1
                faces.append(tuple(conn[pos:pos + n]))
                pos += n
            yield faces

    def polygons(self):
        ''' The boundary faces of more than four nodes, as tuples of nodes '''
        conn = self.faces.get(POLY)
        if not conn:
            return
        pos = 0
        for n in self.face_sizes[POLY]:
            yield tuple(conn[pos:pos + n])
            pos += n

    @property
    def bounds(self):
        n = self.node_count
        if not n:
            return ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
        P = self.nodes
        return (tuple(min(P[3 * i + a] for i in range(n)) for a in range(3)), tuple(max(P[3 * i + a] for i in range(n)) for a in range(3)))

    def cell_ids(self):
        ''' Every cell as (kind, index in its kind): the order the writers number them in '''
        for kind in SOLIDS + (POLY,):
            for i in range(self.cell_count(kind)):
                yield kind, i

    def corners(self, kind, i, faces=False):
        ''' The corner nodes of cell i (or of face i) of that kind (a polyhedron's or a polygon's: its nodes, once each, in the order of its faces) '''
        if kind == POLY:
            if faces:
                for m, fc in enumerate(self.polygons()):
                    if m == i:
                        return fc
                raise IndexError(i)
            for m, fcs in enumerate(self.polyhedra()):
                if m == i:
                    seen = []
                    for fc in fcs:
                        for v in fc:
                            if v not in seen:
                                seen.append(v)
                    return tuple(seen)
            raise IndexError(i)
        n = NODES[kind]
        k = CORNERS[kind]
        conn = (self.faces if faces else self.cells)[kind]
        return tuple(conn[n * i:n * i + k])

    def volume(self):
        ''' The volume of the cells (mm^3) '''
        total = 0.0
        for kind in SOLIDS:
            for i in range(self.cell_count(kind)):
                total += _cell_volume(self.nodes, kind, self.corners(kind, i))
        for faces in self.polyhedra():
            total += _poly_volume(self.nodes, faces)
        return total

    def surface_area(self):
        ''' The area of the boundary faces (mm^2) '''
        total = 0.0
        P = self.nodes
        for kind in SURFACES:
            for i in range(self.face_count(kind)):
                ids = self.corners(kind, i, True)
                ax, ay, az = _polygon_area_vector(P, ids)
                total += math.sqrt(ax * ax + ay * ay + az * az)
        for ids in self.polygons():
            ax, ay, az = _polygon_area_vector(P, ids)
            total += math.sqrt(ax * ax + ay * ay + az * az)
        return total

    def __repr__(self):
        parts = ['{:,} {}'.format(self.cell_count(k), k) for k in SOLIDS + (POLY,) if self.cell_count(k)]
        faces = ['{:,} {}'.format(self.face_count(k), k) for k in SURFACES + (POLY,) if self.face_count(k)]
        zones = ['{} ({:,})'.format(n, self.face_count(zone=i)) for i, n in enumerate(self.zone_names) if self.face_count(zone=i)]
        return 'Mesh {}: {} cells{}, {:,} nodes; boundary {}{}'.format(
            self.name, ', '.join(parts) or 'no', '' if not self.element_size else ' (element size about {:.4g} mm)'.format(self.element_size), self.node_count,
            ', '.join(faces) or 'none', '; zones ' + ', '.join(zones) if zones else '')

    # --- what can be done with it
    def quality(self, full=False, max_cells=150000):
        ''' How good the elements are (MeshQuality).  The numbers that look at the neighbours of an element are made for meshes of up to `max_cells` cells,
            and for a larger one -- they take a dictionary of its faces -- when full=True is asked; the others are made for every `k`-th cell of a larger mesh '''
        return _measure(self, full, max_cells)

    def export(self, path, format=None, **options):
        ''' Writes the mesh: format 'gmsh' (.msh, version 2.2), 'vtk' (.vtu), 'abaqus' (.inp), 'nastran' (.bdf), 'unv' (.unv), 'fluent' (.msh: Ansys Fluent's mesh),
            'openfoam' (a polyMesh folder: `path` is the folder), 'stl' / 'obj' (the surface).  The format is found from the ending of the path when it is not given.
            options: scale (a factor for the coordinates: 0.001 for metres), zones=False (without the zones), and, for the formats that have them, the ones they list '''
        from fieldes.stdlib import meshwriters
        return meshwriters.write_mesh(self, path, format, **options)

    def surface_mesh(self):
        ''' The boundary alone, as a mesh '''
        out = Mesh(self.name + '_surface')
        out.nodes = array('d', self.nodes)
        out.faces = {k: array('i', v) for k, v in self.faces.items()}
        out.face_sizes = {k: array('i', v) for k, v in self.face_sizes.items()}
        out.face_zone = {k: array('i', v) for k, v in self.face_zone.items()}
        out.zone_names = list(self.zone_names)
        out.element = self.element
        out.element_size = self.element_size
        out.body = self.body
        out.box = self.box
        return out

    def _display(self):
        ''' What FielDes draws for a mesh stated on its own: the body, with a result card whose "Elements" show the cells, each coloured by its size, skewness, aspect ratio
            or orthogonal quality (a surface mesh: its triangles or quadrilaterals, each coloured by its size) '''
        if self._shown is None:
            self._shown = _display_of(self)
        return self._shown


################################################################################
# Geometry of cells

def _polygon_area_vector(P, ids):
    ''' The area vector (the cross products of the fan from the first corner, halved) of a face: its normal times its area '''
    x0, y0, z0 = P[3 * ids[0]], P[3 * ids[0] + 1], P[3 * ids[0] + 2]
    sx = sy = sz = 0.0
    for k in range(1, len(ids) - 1):
        a, b = ids[k], ids[k + 1]
        ux, uy, uz = P[3 * a] - x0, P[3 * a + 1] - y0, P[3 * a + 2] - z0
        vx, vy, vz = P[3 * b] - x0, P[3 * b + 1] - y0, P[3 * b + 2] - z0
        sx += uy * vz - uz * vy
        sy += uz * vx - ux * vz
        sz += ux * vy - uy * vx
    return 0.5 * sx, 0.5 * sy, 0.5 * sz


def _centroid(P, ids):
    n = len(ids)
    if n == 3:
        a, b, c = ids
        return ((P[3 * a] + P[3 * b] + P[3 * c]) / 3.0, (P[3 * a + 1] + P[3 * b + 1] + P[3 * c + 1]) / 3.0, (P[3 * a + 2] + P[3 * b + 2] + P[3 * c + 2]) / 3.0)
    return (sum(P[3 * i] for i in ids) / n, sum(P[3 * i + 1] for i in ids) / n, sum(P[3 * i + 2] for i in ids) / n)


def _poly_volume(P, faces):
    ''' The volume of a polyhedron from its faces (tuples of nodes, turned outward): the sum over the triangles of their fans of the signed volume of the cone on one of its own corners (a cone on the origin of
        the coordinates gives the same for a closed polyhedron, but a polyhedron that is not closed -- the flat piece of two quadrilaterals -- then has a volume of its distance from the origin) -- right for any closed
        polyhedron, positive when its faces are turned outward '''
    total = 0.0
    r = faces[0][0]
    rx, ry, rz = P[3 * r], P[3 * r + 1], P[3 * r + 2]
    for fc in faces:
        a = fc[0]
        ax, ay, az = P[3 * a] - rx, P[3 * a + 1] - ry, P[3 * a + 2] - rz
        for m in range(1, len(fc) - 1):
            b, c = fc[m], fc[m + 1]
            bx, by, bz = P[3 * b] - rx, P[3 * b + 1] - ry, P[3 * b + 2] - rz
            cx, cy, cz = P[3 * c] - rx, P[3 * c + 1] - ry, P[3 * c + 2] - rz
            total += (ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx)) / 6.0
    return total


def _poly_centroid(P, faces):
    ''' The middle of a polyhedron's volume (the cones of the triangles of its faces on the middle of its nodes, weighted by their volumes) '''
    nodes = []
    for fc in faces:
        for v in fc:
            if v not in nodes:
                nodes.append(v)
    ox, oy, oz = _centroid(P, nodes)
    total = sx = sy = sz = 0.0
    for fc in faces:
        a = fc[0]
        for m in range(1, len(fc) - 1):
            b, c = fc[m], fc[m + 1]
            ux, uy, uz = P[3 * a] - ox, P[3 * a + 1] - oy, P[3 * a + 2] - oz
            vx, vy, vz = P[3 * b] - ox, P[3 * b + 1] - oy, P[3 * b + 2] - oz
            wx, wy, wz = P[3 * c] - ox, P[3 * c + 1] - oy, P[3 * c + 2] - oz
            vol = (ux * (vy * wz - vz * wy) + uy * (vz * wx - vx * wz) + uz * (vx * wy - vy * wx)) / 6.0
            total += vol
            sx += vol * (ux + vx + wx) * 0.25
            sy += vol * (uy + vy + wy) * 0.25
            sz += vol * (uz + vz + wz) * 0.25
    if abs(total) < 1e-300:
        return (ox, oy, oz)
    return (ox + sx / total, oy + sy / total, oz + sz / total)


def _cell_volume(P, kind, ids):
    ''' The volume of a cell, from the cone of its faces on its middle: right for any element with plane or nearly plane faces, positive when it is the right way out '''
    if kind in ('tet4', 'tet10'):
        return _det6(P, ids[0], ids[1], ids[2], ids[3]) / 6.0
    cx, cy, cz = _centroid(P, ids)
    total = 0.0
    for face in FACES[kind]:
        fid = [ids[i] for i in face]
        fx, fy, fz = _centroid(P, fid)
        ax, ay, az = _polygon_area_vector(P, fid)
        total += ax * (fx - cx) + ay * (fy - cy) + az * (fz - cz)
    return total / 3.0


################################################################################
# Meshing

_MIRRORED = [0]             # the planes the last mesh of _kernel_tets was mirrored about (H226): 0 for a part that has no symmetry about a plane through the middle of its box


def _kernel_tets(field, lo, hi, h, snap=True, symmetric_mend=None):
    ''' The kernel's conforming tetrahedra of the inside of a body: (nodes, tetrahedra) as flat arrays.  `symmetric_mend` 0 or 1 (H226, the user, 2026-11-16: "I meshed the bracket myself from a file and the mesh is just
        not symmetrical, despite the part being symmetrical"): a part that is symmetric about a plane through the middle of its box is meshed as the mirror image of a half that was mended before it was mirrored (1: mended),
        as an analysis does it -- the mesh is then symmetric and needs no mending after it: _MIRRORED says how many planes it was mirrored about, and 0 when the mesh is the plain one that is mended after, as before '''
    _need_kernel()
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i], hi[i]
    _MIRRORED[0] = 0
    # (a mesh that is refined after this one -- a graded mesh -- is cleaned up once, after the refinement: _snap; on the lattice before it the passes made the mesh of example 36 worse, H223)
    symmetric = snap and symmetric_mend is not None and getattr(lib, 'libfive_tetmesh_new_symmetric', None) is not None
    if symmetric:
        ptr = lib.libfive_tetmesh_new_symmetric(field.ptr, region, float(h), 1 if symmetric_mend else 0)
    else:
        make = lib.libfive_tetmesh_new if snap or getattr(lib, 'libfive_tetmesh_new_plain', None) is None else lib.libfive_tetmesh_new_plain
        ptr = make(field.ptr, region, float(h))
    if not ptr:
        message = lib.libfive_tetmesh_last_message()
        raise MeshError('the body cannot be meshed' + (': ' + message.decode('utf-8', 'replace') if message else ''))
    if symmetric:
        _MIRRORED[0] = int(lib.libfive_tetmesh_mirrored(ptr))
    try:
        counts = (ctypes.c_int64 * 3)()
        lib.libfive_tetmesh_counts(ptr, counts)
        nv, nt = int(counts[0]), int(counts[1])
        verts = (ctypes.c_double * (3 * nv))()
        tets = (ctypes.c_int32 * (4 * nt))()
        lib.libfive_tetmesh_vertices(ptr, verts)
        lib.libfive_tetmesh_tets(ptr, tets)
        P = array('d')
        P.frombytes(bytes(verts))
        T = array('i')
        T.frombytes(bytes(tets))
    finally:
        lib.libfive_tetmesh_delete(ptr)
    return P, T


def _boundary_nodes(tets):
    ''' The nodes of the faces that only one tetrahedron has: the nodes on the surface '''
    count = {}
    for t in tets:
        if t is None:
            continue
        a, b, c, d = t
        for f in ((a, b, c), (a, b, d), (a, c, d), (b, c, d)):
            k = tuple(sorted(f))
            count[k] = count.get(k, 0) + 1
    nodes = set()
    for k, n in count.items():
        if n == 1:
            nodes.update(k)
    return nodes


def _snap_to_surface(field, points, scales, iterations=3):
    ''' The points moved onto the surface of the field (its value 0), by Newton steps along its gradient, no further than half of `scales` at a time '''
    cur = [list(p) for p in points]
    for _ in range(iterations):
        samples = []
        steps = []
        for p, s in zip(cur, scales):
            e = max(1e-7, 0.02 * s)
            steps.append(e)
            samples.append((p[0], p[1], p[2]))
            samples.append((p[0] + e, p[1], p[2]))
            samples.append((p[0] - e, p[1], p[2]))
            samples.append((p[0], p[1] + e, p[2]))
            samples.append((p[0], p[1] - e, p[2]))
            samples.append((p[0], p[1], p[2] + e))
            samples.append((p[0], p[1], p[2] - e))
        values = _eval(field, samples)
        for i, (p, s) in enumerate(zip(cur, scales)):
            v = values[7 * i:7 * i + 7]
            e = steps[i]
            gx, gy, gz = (v[1] - v[2]) / (2 * e), (v[3] - v[4]) / (2 * e), (v[5] - v[6]) / (2 * e)
            g2 = gx * gx + gy * gy + gz * gz
            if g2 < 1e-24:
                continue
            f = v[0] / g2
            dx, dy, dz = f * gx, f * gy, f * gz
            length = math.sqrt(dx * dx + dy * dy + dz * dz)
            limit = 0.5 * s
            if length > limit:
                k = limit / length
                dx, dy, dz = dx * k, dy * k, dz * k
            p[0] -= dx
            p[1] -= dy
            p[2] -= dz
    return cur


def _point(P, i):
    return (P[3 * i], P[3 * i + 1], P[3 * i + 2])


def _sub3(u, v):
    return (u[0] - v[0], u[1] - v[1], u[2] - v[2])


def _cross3(u, v):
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


def _dot3(u, v):
    return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]


def _field_gradients(field, points, scales):
    ''' The value and the gradient of the field at each of the points, by central differences of 2 % of the scale there: [(value, (gx, gy, gz))] '''
    samples = []
    steps = []
    for p, s in zip(points, scales):
        e = max(1e-7, 0.02 * s)
        steps.append(e)
        x, y, z = p
        samples.extend(((x, y, z), (x + e, y, z), (x - e, y, z), (x, y + e, z), (x, y - e, z), (x, y, z + e), (x, y, z - e)))
    values = _eval(field, samples)
    out = []
    for i, e in enumerate(steps):
        v = values[7 * i:7 * i + 7]
        out.append((v[0], ((v[1] - v[2]) / (2 * e), (v[3] - v[4]) / (2 * e), (v[5] - v[6]) / (2 * e))))
    return out


def _solve3(A, b):
    ''' x of A x = b for a 3 x 3 matrix (Cramer's rule); None when there is no single answer '''
    def det(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    d = det(A)
    if abs(d) < 1e-18:
        return None
    out = []
    for k in range(3):
        m = [row[:] for row in A]
        for r in range(3):
            m[r][k] = b[r]
        out.append(det(m) / d)
    return out


def _unit_sum(vectors):
    sx = sum(v[0] for v in vectors)
    sy = sum(v[1] for v in vectors)
    sz = sum(v[2] for v in vectors)
    length = math.sqrt(sx * sx + sy * sy + sz * sz)
    return (sx / length, sy / length, sz / length) if length > 1e-12 else (0.0, 0.0, 1.0)


def _two_planes(p, A, B):
    ''' The point of the line where the planes A and B (each (count, normal, offset)) meet that is nearest to `p`; None when they are one plane '''
    nA, dA, nB, dB = A[1], A[2], B[1], B[2]
    c = nA[0] * nB[0] + nA[1] * nB[1] + nA[2] * nB[2]
    det = 1.0 - c * c
    if det < 1e-6:
        return None
    rA = dA - (nA[0] * p[0] + nA[1] * p[1] + nA[2] * p[2])
    rB = dB - (nB[0] * p[0] + nB[1] * p[1] + nB[2] * p[2])
    a = (rA - c * rB) / det
    b = (rB - c * rA) / det
    return tuple(p[i] + a * nA[i] + b * nB[i] for i in range(3))


def _split_creases(P, tets, field, notes, sizer, rounds=6):
    ''' The corners that the surface cuts across given nodes of their own.  A boundary edge whose two ends are on different faces of the body -- a node on the wall, a node on the floor -- is a chord
        across the corner between them, and the faces on it slope across the body (the teeth along the rim of a post in the channel of examples/36_meshing_for_cfd.py: the user, 2026-11-13,
        "absolutely awful"); the lattice and the bisection put nodes on each face and, between them, none on the crease.  Each such edge is split in two -- every tetrahedron round it, so the mesh stays
        conforming -- and the new node goes to the crease, the point of the line where the two faces' planes meet that is nearest to the middle of the edge, in the steps of _fit_surface (the
        whole way, half or a quarter, whichever turns no tetrahedron inside out; the nodes round it have moved by the next round).  An edge is split only while it cuts the corner by more than
        a twentieth of its length and is longer than four tenths of the size asked for there (`sizer`): without that the corner ends in a cluster of faces a few hundredths of a millimetre
        across.  (Tried and taken back: refusing a node nearer than 0.35 of the size to another one -- the skewness of the example went from 0.889 to 0.970 and 33 faces were off the surface, not 19.)  Returns the tetrahedra '''
    cos_crease = math.cos(math.radians(40.0))
    T = [tuple(t) for t in tets]
    total = 0
    moved_ends = 0
    for _ in range(rounds):
        done = 0
        edges, faces = _boundary_edges(T)
        near = {}
        for f in faces:
            for i in range(3):
                near.setdefault(f[i], set()).add(f[(i + 1) % 3])
                near.setdefault(f[(i + 1) % 3], set()).add(f[i])
        nodes = sorted(near)
        length = {v: sum(math.dist(_point(P, v), _point(P, u)) for u in near[v]) / len(near[v]) for v in nodes}
        grads = dict(zip(nodes, _field_gradients(field, [_point(P, v) for v in nodes], [length[v] for v in nodes])))
        strengths = sorted(math.sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) for _, g in grads.values())
        typical = strengths[len(strengths) // 2] if strengths else 1.0
        chords = []
        for a, b in sorted(edges):
            (va, ga), (vb, gb) = grads[a], grads[b]
            la, lb = math.sqrt(_dot3(ga, ga)), math.sqrt(_dot3(gb, gb))
            if la < 0.9 * typical or lb < 0.9 * typical or la <= 1e-12 or lb <= 1e-12:
                continue                                        # (an end on a crease: the edge runs along it)
            na = (ga[0] / la, ga[1] / la, ga[2] / la)
            nb = (gb[0] / lb, gb[1] / lb, gb[2] / lb)
            if _dot3(na, nb) >= cos_crease:
                continue
            pa, pb = _point(P, a), _point(P, b)
            fa = tuple(pa[i] - va * ga[i] / (la * la) for i in range(3))        # (the points of the surface under the ends: the planes go through them)
            fb = tuple(pb[i] - vb * gb[i] / (lb * lb) for i in range(3))
            middle = tuple(0.5 * (pa[i] + pb[i]) for i in range(3))
            x = _two_planes(middle, (1, na, _dot3(na, fa)), (1, nb, _dot3(nb, fb)))
            if x is None or math.dist(x, middle) > math.dist(pa, pb):
                continue
            chords.append((a, b, x, middle))
        if chords:
            # (it must be worth it: the chord cuts the corner by more than a twentieth of its length, and it is not already shorter than four tenths of the size asked for)
            sizes = sizer([c[3] for c in chords])
            depth = _field_gradients(field, [c[3] for c in chords], [max(float(z), 1e-6) for z in sizes])
            worth = []
            for (a, b, x, middle), size, (value, g) in zip(chords, sizes, depth):
                gl = math.sqrt(_dot3(g, g))
                cut = abs(value) / gl if gl > 1e-12 else 0.0
                length_ab = math.dist(_point(P, a), _point(P, b))
                if cut > 0.05 * length_ab and length_ab > 0.4 * float(size):
                    worth.append((a, b, x))
            chords = worth
        if not chords:
            break
        edge_tets = {}
        for i, t in enumerate(T):
            if t is not None:
                for p, q in _PAIRS:
                    edge_tets.setdefault(_edge(t[p], t[q]), set()).add(i)
        for a, b, x in chords:
            e = _edge(a, b)
            if e not in edge_tets:
                continue
            # (the node is put on the corner at once -- a node that is moved there in steps stops half way, off the corner and off the face, and the tetrahedra round it are slivers -- and the edge
            # is split only if every tetrahedron round it stays right-handed and none is poorer than 0.1 with the node there)
            m = len(P) // 3
            P.extend(x)
            children = []
            for u in edge_tets[e]:
                t = T[u]
                children.append((u, (tuple(m if v == b else v for v in t), tuple(m if v == a else v for v in t))))
            if any(_tet_ratio(P, child) < 0.03 for _, pair in children for child in pair):
                del P[3 * m:]
                # The edge cannot be split (the tetrahedra round it are too flat for a node on the corner).  Then one end goes to the corner instead -- the end whose move leaves the tetrahedra round it
                # best -- so that the faces on the edge are on one side of the corner or the other, not across it (a chamfer: the ledge in the ring of prisms at the ceiling, H193)
                around_end = {}
                for t in T:
                    if t is not None:
                        for w in t:
                            if w in (a, b):
                                around_end.setdefault(w, []).append(t)
                best_end = None
                for w in (a, b):
                    ts = around_end.get(w, ())
                    old = _point(P, w)
                    before = min((_tet_ratio(P, t) for t in ts), default=1.0)
                    P[3 * w], P[3 * w + 1], P[3 * w + 2] = x
                    now = min((_tet_ratio(P, t) for t in ts), default=1.0)
                    P[3 * w], P[3 * w + 1], P[3 * w + 2] = old
                    if now > 0.05 and now >= 0.5 * min(before, 0.3) and (best_end is None or now > best_end[0]):
                        best_end = (now, w)
                if best_end is not None:
                    w = best_end[1]
                    P[3 * w], P[3 * w + 1], P[3 * w + 2] = x
                    moved_ends += 1
                    done += 1
                continue
            for u, pair in children:
                t = T[u]
                for p, q in _PAIRS:
                    edge_tets[_edge(t[p], t[q])].discard(u)
                T[u] = None
                for child in pair:
                    T.append(child)
                    for p, q in _PAIRS:
                        edge_tets.setdefault(_edge(child[p], child[q]), set()).add(len(T) - 1)
            del edge_tets[e]
            total += 1
            done += 1
        T = [t for t in T if t is not None]
        if not done:
            break
    if total:
        notes.append('{:,} edges across the corners of the body split, the new nodes put on the corners'.format(total))
    if moved_ends:
        notes.append('{:,} nodes next to a corner that could not be split put on it'.format(moved_ends))
    return T


def _fit_surface(P, tets, field, notes, rounds=8, floor=0.1, goals=None):
    ''' The nodes of the surface that are not on it put on it.  A node that the refinement made in the middle of an edge of the surface goes onto the surface, but a node whose move would turn a
        tetrahedron inside out is put back where it was -- in the middle of the edge, off the surface, and it stays there as a corner of a face that slopes across the body: the teeth along the rim
        of a post in the channel of examples/36_meshing_for_cfd.py (the user, 2026-11-13: "absolutely awful"; 63 of 4,168 faces of its surface were more than a twentieth of their size off it,
        the worst 0.43 mm).  Here each such node, the worst first, goes the whole way to the surface, or half of it, or a quarter, whichever turns no tetrahedron inside out and leaves
        none worse than `floor` (or than it was); the others round it have moved by the next round, and so on up to `rounds`.  A node that is on a crease is left to the edge pass of the kernel (_snap) '''
    moved_total = 0
    for _ in range(rounds):
        _, faces = _boundary_edges(tets)
        if not faces:
            break
        near = {}
        for f in faces:
            for i in range(3):
                a, b = f[i], f[(i + 1) % 3]
                near.setdefault(a, set()).add(b)
                near.setdefault(b, set()).add(a)
        nodes = sorted(near)
        length = {v: sum(math.dist(_point(P, v), _point(P, u)) for u in near[v]) / len(near[v]) for v in nodes}
        grads = _field_gradients(field, [_point(P, v) for v in nodes], [length[v] for v in nodes])
        strengths = sorted(math.sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) for _, g in grads)
        typical = strengths[len(strengths) // 2] if strengths else 1.0
        off = []
        for v, (value, g) in zip(nodes, grads):
            gl = math.sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2])
            if gl < 0.5 * typical or gl <= 1e-12:
                continue                                        # (a crease: the gradient of the field is not one direction there)
            if abs(value) / gl > 0.03 * length[v]:
                off.append((abs(value) / gl, v))
        if not off:
            break
        off.sort(reverse=True)
        wanted = [v for _, v in off]
        info = {v: (value, g) for v, (value, g) in zip(nodes, grads)}
        projections = _snap_to_surface(field, [_point(P, v) for v in wanted], [length[v] for v in wanted], iterations=4)
        cos_crease = math.cos(math.radians(40.0))
        goals_of = {}
        crease_goals = goals
        for v, projection in zip(wanted, projections):
            # (where it is wanted: first on the plane that most of the faces round it are on -- the plane of the ceiling for a node that the bisection left beside the wall and below the ceiling,
            # which the nearest surface, the wall, would pull the wrong way and turn the tetrahedra between inside out -- then the nearest point of the surface)
            groups = []
            for u in near[v]:
                value_u, g_u = info[u]
                gl_u = math.sqrt(g_u[0] * g_u[0] + g_u[1] * g_u[1] + g_u[2] * g_u[2])
                if gl_u < 0.9 * typical or gl_u <= 1e-12:
                    continue
                n_u = (g_u[0] / gl_u, g_u[1] / gl_u, g_u[2] / gl_u)
                pu = _point(P, u)
                foot = tuple(pu[i] - value_u * g_u[i] / (gl_u * gl_u) for i in range(3))
                for group in groups:
                    m = _unit_sum([x[0] for x in group])
                    if _dot3(n_u, m) >= cos_crease:
                        group.append((n_u, foot))
                        break
                else:
                    groups.append([(n_u, foot)])
            groups.sort(key=len, reverse=True)
            p = _point(P, v)
            wanted_at = []
            for group in groups[:2]:
                n = _unit_sum([x[0] for x in group])
                d = sum(_dot3(n, x[1]) for x in group) / len(group)
                h = _dot3(n, p) - d
                wanted_at.append(tuple(p[i] - h * n[i] for i in range(3)))
            wanted_at.append(tuple(projection))
            if crease_goals and v in crease_goals:
                wanted_at.insert(0, crease_goals[v])         # (a node that was put in the middle of a chord across a corner: on the corner)
            goals_of[v] = wanted_at
        around = _tets_around(tets)
        moved = 0
        for v in wanted:
            ts = around.get(v, ())
            old = _point(P, v)
            before = min((_tet_ratio(P, t) for t in ts), default=1.0)
            done = False
            for goal in goals_of[v]:
                for share in (1.0, 0.5, 0.25):
                    P[3 * v], P[3 * v + 1], P[3 * v + 2] = (old[0] + share * (goal[0] - old[0]), old[1] + share * (goal[1] - old[1]), old[2] + share * (goal[2] - old[2]))
                    now = min((_tet_ratio(P, t) for t in ts), default=1.0)
                    if now > 0.0 and now >= min(floor, before):
                        done = True
                        break
                if done:
                    break
            if done:
                moved += 1
            else:
                P[3 * v], P[3 * v + 1], P[3 * v + 2] = old
        moved_total += moved
        if not moved:
            break
    if moved_total:
        notes.append('{:,} nodes of the surface that were off it put on it'.format(moved_total))


def _lift_caps(P, tets, notes, rounds=3, limit=0.6):
    ''' The CAPS lifted off the surface.  A poor tetrahedron (mean ratio under `limit`, the mender's own) with three nodes on the surface and the fourth a little way inside -- on the wall of a round hole,
        whose nodes lie on the circle -- is a flat cap: the worst skewness of the plate with a hole of radius 2.5 at 2 mm is 0.96 on one, with a mean ratio of 0.5.  The mender's swaps and collapses do
        not take it out; the node inside is moved away from the surface along the normal of the base, to the height of a regular tetrahedron on it (0.8 of an edge), when the tetrahedra round that node
        are better for it (the worst of them).  Returns the tetrahedra '''
    total = 0
    for _ in range(rounds):
        _, faces = _boundary_edges(tets)
        on_surface = set(v for f in faces for v in f)
        around = None                   # (made when a cap is found, and kept for the next round: see _tets_around)
        changed = 0
        for t in tets:
            on = [v for v in t if v in on_surface]
            if len(on) != 3:
                continue
            inside = [v for v in t if v not in on_surface]
            if len(inside) != 1 or _tet_ratio(P, t) >= limit:
                continue
            c = inside[0]
            a, b, d = on
            pa, pb, pd, pc = _point(P, a), _point(P, b), _point(P, d), _point(P, c)
            n = _cross3(_sub3(pb, pa), _sub3(pd, pa))
            size = math.sqrt(_dot3(n, n))
            if size <= 0.0:
                continue
            n = (n[0] / size, n[1] / size, n[2] / size)
            h = _dot3(_sub3(pc, pa), n)
            if h < 0.0:
                n = (-n[0], -n[1], -n[2])
                h = -h
            edge = (math.dist(pa, pb) + math.dist(pb, pd) + math.dist(pd, pa)) / 3.0
            target = 0.8 * edge
            if h >= target:
                continue
            if around is None:
                around = _tets_around(tets)
            ts = around.get(c, ())
            before = min((_tet_ratio(P, u) for u in ts), default=1.0)
            for share in (1.0, 0.5):
                P[3 * c], P[3 * c + 1], P[3 * c + 2] = (pc[0] + share * (target - h) * n[0], pc[1] + share * (target - h) * n[1], pc[2] + share * (target - h) * n[2])
                now = min((_tet_ratio(P, u) for u in ts), default=1.0)
                if now > before + 1e-6:
                    changed += 1
                    break
            else:
                P[3 * c], P[3 * c + 1], P[3 * c + 2] = pc
        total += changed
        if not changed:
            break
    if total:
        notes.append('{:,} nodes under a cap on the surface lifted off it'.format(total))
    return tets


def _snap(P, tets, field, notes, rim_floor=0.3):
    ''' The surface of a tetrahedral mesh cleaned up by the kernel (kernel/src/fea/tetmesh.cpp snapSurface -- the one pass that meshShape makes on its own mesh, so that an analysis and mesh_tet4 have one mesher, H223): the
        EDGES and CORNERS of the body that the lattice cut across put back (the nodes of the faces that lean away from the field are put on the line where two planes meet, or the corner of three), then the nodes on the
        RIM of a flat face -- the circle where a hole meets the face of a plate -- put on it.  A uniform mesh has had it from the kernel when it was made; this is for a mesh that was refined after that (a graded one:
        the nodes that the bisection puts on the surface).  `rim_floor`: the poorest mean ratio that a move onto a rim may leave a tetrahedron (0.3; 0.1 for a graded mesh, where the layers meet the walls in flat
        tetrahedra).  P (an array of the nodes) is changed in place; returns the tetrahedra '''
    if getattr(lib, 'libfive_tetmesh_snap', None) is None:
        raise MeshError('this FielDes library is too old to put the nodes of a mesh on its edges')
    T = array('i')
    for t in tets:
        T.extend(t)
    nodes = (ctypes.c_double * len(P)).from_buffer_copy(P)
    cells = (ctypes.c_int32 * len(T)).from_buffer_copy(T)
    counts_ = (ctypes.c_int64 * 2)()
    ptr = lib.libfive_tetmesh_snap(field.ptr, len(P) // 3, nodes, len(T) // 4, cells, float(rim_floor), counts_)
    if not ptr:
        message = lib.libfive_tetmesh_last_message()
        raise MeshError('the surface of the mesh could not be cleaned up' + (': ' + message.decode('utf-8', 'replace') if message else ''))
    try:
        counts = (ctypes.c_int64 * 3)()
        lib.libfive_tetmesh_counts(ptr, counts)
        nv, nt = int(counts[0]), int(counts[1])
        verts = (ctypes.c_double * (3 * nv))()
        out = (ctypes.c_int32 * (4 * nt))()
        lib.libfive_tetmesh_vertices(ptr, verts)
        lib.libfive_tetmesh_tets(ptr, out)
        new_nodes = array('d')
        new_nodes.frombytes(bytes(verts))
        new_cells = array('i')
        new_cells.frombytes(bytes(out))
    finally:
        lib.libfive_tetmesh_delete(ptr)
    P[:] = new_nodes
    edges, rims = int(counts_[0]), int(counts_[1])
    if edges:
        notes.append('{:,} nodes of the surface were put on the edges and corners of the body that the lattice had cut across'.format(edges))
    if rims:
        notes.append('{:,} nodes on the rim of a flat face put on it'.format(rims))
    return [tuple(new_cells[4 * i:4 * i + 4]) for i in range(nt)]


def _refine_to_size(P, tets, sizer, field, ratio=1.3, max_passes=30, max_tets=3000000, notes=None):
    ''' The tetrahedra bisected -- the longest edge of one at a time, and of all that share the edge, so that the mesh stays conforming (Rivara's longest-edge
        bisection) -- until their longest edges are no more than `ratio` times the size asked for where they are.  The vertices that the bisection puts on the boundary
        are moved onto the surface.  Returns the tetrahedra (a list of tuples) '''
    T = [tuple(tets[4 * i:4 * i + 4]) for i in range(len(tets) // 4)]

    def longest(t):
        best = None
        for i, j in _PAIRS:
            a, b = t[i], t[j]
            dx, dy, dz = P[3 * a] - P[3 * b], P[3 * a + 1] - P[3 * b + 1], P[3 * a + 2] - P[3 * b + 2]
            key = (dx * dx + dy * dy + dz * dz, (a, b) if a < b else (b, a))
            if best is None or key > best:
                best = key
        return best

    refined = 0
    for npass in range(max_passes):
        live = [i for i, t in enumerate(T) if t is not None]
        centres = []
        for i in live:
            a, b, c, d = T[i]
            centres.append(((P[3 * a] + P[3 * b] + P[3 * c] + P[3 * d]) * 0.25, (P[3 * a + 1] + P[3 * b + 1] + P[3 * c + 1] + P[3 * d + 1]) * 0.25,
                            (P[3 * a + 2] + P[3 * b + 2] + P[3 * c + 2] + P[3 * d + 2]) * 0.25))
        wanted = sizer(centres)
        marked = [i for i, s in zip(live, wanted) if longest(T[i])[0] > (ratio * s) ** 2]
        if not marked:
            break
        if len(live) + 3 * len(marked) > max_tets:
            raise MeshError('the mesh would have more than {:,} tetrahedra: ask for a larger element size'.format(max_tets))
        edge_tets = {}
        for i in live:
            t = T[i]
            for p, q in _PAIRS:
                edge_tets.setdefault(_edge(t[p], t[q]), set()).add(i)
        first_new = len(P) // 3

        def bisect(edge):
            a, b = edge
            m = len(P) // 3
            P.extend(((P[3 * a] + P[3 * b]) * 0.5, (P[3 * a + 1] + P[3 * b + 1]) * 0.5, (P[3 * a + 2] + P[3 * b + 2]) * 0.5))
            for u in list(edge_tets[edge]):
                t = T[u]
                for p, q in _PAIRS:
                    edge_tets[_edge(t[p], t[q])].discard(u)
                T[u] = None
                for child in (tuple(m if v == b else v for v in t), tuple(m if v == a else v for v in t)):
                    T.append(child)
                    n = len(T) - 1
                    for p, q in _PAIRS:
                        edge_tets.setdefault(_edge(child[p], child[q]), set()).add(n)
            del edge_tets[edge]

        def propagate(t):
            while True:
                _, e = longest(T[t])
                other = None
                for u in edge_tets[e]:
                    if u != t and longest(T[u])[1] != e:
                        other = u
                        break
                if other is None:
                    break
                t = other
            bisect(e)

        for t0 in marked:
            guard = 0
            while T[t0] is not None and guard < 60:
                propagate(t0)
                guard += 1
        refined += len(marked)
        # The vertices the bisection put on the boundary go to the surface
        boundary = _boundary_nodes(T)
        moved = [n for n in range(first_new, len(P) // 3) if n in boundary]
        if moved:
            points = [(P[3 * n], P[3 * n + 1], P[3 * n + 2]) for n in moved]
            scales = sizer(points)
            target = _snap_to_surface(field, points, scales)
            old = {n: p for n, p in zip(moved, points)}
            for n, p in zip(moved, target):
                P[3 * n], P[3 * n + 1], P[3 * n + 2] = p
            _revert_inverted(P, T, set(moved), old)
    live_tets = [t for t in T if t is not None]
    if notes is not None:
        notes.append('refined to the size field in {} passes ({:,} bisections of tetrahedra)'.format(npass + 1, refined))
    return live_tets


def _revert_inverted(P, T, moved, old):
    ''' Moved nodes whose tetrahedra no longer have a volume go back to where they were '''
    for _ in range(3):
        bad = set()
        for t in T:
            if t is None:
                continue
            if any(v in moved for v in t) and _det6(P, t[0], t[1], t[2], t[3]) <= 0.0:
                bad.update(v for v in t if v in moved)
        if not bad:
            return
        for n in bad:
            P[3 * n], P[3 * n + 1], P[3 * n + 2] = old[n]
            moved.discard(n)


def _tet_ratio(P, t):
    ''' The mean ratio of a tetrahedron, 12 (3 V)^(2/3) over the sum of its squared edges: 1 for a regular one, 0 for one with no volume (-1 inside out) '''
    a, b, c, d = t
    v = _det6(P, a, b, c, d) / 6.0
    if v <= 0.0:
        return -1.0
    s = 0.0
    for i, j in _PAIRS:
        p, q = t[i], t[j]
        dx, dy, dz = P[3 * p] - P[3 * q], P[3 * p + 1] - P[3 * q + 1], P[3 * p + 2] - P[3 * q + 2]
        s += dx * dx + dy * dy + dz * dz
    return 12.0 * (3.0 * v) ** (2.0 / 3.0) / s


def _mend(P, tets, field, sweeps, notes, rounds=2, split_badness=100.0):
    ''' The poor tetrahedra mended in the kernel (kernel/src/fea/tetmender.cpp) by the passes of Netgen's volume optimization -- the logic of Salome's tetrahedral mesher, read in its open source
        (meshfunc.cpp OptimizeVolume, improve3.cpp, smoothing3.cpp, improve2.cpp; nothing of its code is used), in `rounds` rounds of: the nodes on the surface moved along it and back onto the
        surface of `field`; the needle triangles of the surface (a smallest angle under 30 degrees) flipped where the quadrilateral is convex and the old and the new triangles are within 30 degrees of
        the surface's own normal, the tetrahedra under the new edge made again; edges and faces swapped and interior nodes taken into a neighbour where that makes the worst tetrahedron round them
        better; the edges of the very worst tetrahedra (a badness of `split_badness` or more) split; and each interior node of a poor tetrahedron moved to where the SUM of the badness of its
        tetrahedra is least (`sweeps` sweeps of it in each pass).  The badness is Netgen's: (0.0080187537 (the sum of the squared edges)^1.5 / V)^2, 1 for a regular tetrahedron, the cube of the
        inverse of the mean ratio.  Nothing moves or changes unless it makes the tetrahedra round it better, and a node is never left with a poorer worst tetrahedron.  `field` None: the surface is
        left as it is.  P (an array of the nodes) is changed in place and numbered again (the nodes that no tetrahedron uses are gone, the nodes of a split are new); returns the tetrahedra '''
    if getattr(lib, 'libfive_tetmesh_mend', None) is None:
        raise MeshError('this FielDes library is too old to mend a mesh')
    T = array('i')
    for t in tets:
        T.extend(t)
    nodes = (ctypes.c_double * len(P)).from_buffer_copy(P)
    cells = (ctypes.c_int32 * len(T)).from_buffer_copy(T)
    counts_ = (ctypes.c_int64 * 4)()
    ptr = lib.libfive_tetmesh_mend(field.ptr if field is not None else None, len(P) // 3, nodes, len(T) // 4, cells, int(rounds), int(sweeps), float(split_badness),
                                   1 if field is not None else 0, counts_)
    if not ptr:
        message = lib.libfive_tetmesh_last_message()
        raise MeshError('the mesh could not be mended' + (': ' + message.decode('utf-8', 'replace') if message else ''))
    try:
        counts = (ctypes.c_int64 * 3)()
        lib.libfive_tetmesh_counts(ptr, counts)
        nv, nt = int(counts[0]), int(counts[1])
        verts = (ctypes.c_double * (3 * nv))()
        out = (ctypes.c_int32 * (4 * nt))()
        lib.libfive_tetmesh_vertices(ptr, verts)
        lib.libfive_tetmesh_tets(ptr, out)
        new_nodes = array('d')
        new_nodes.frombytes(bytes(verts))
        new_cells = array('i')
        new_cells.frombytes(bytes(out))
    finally:
        lib.libfive_tetmesh_delete(ptr)
    P[:] = new_nodes
    swaps, flips, splits, moved = (int(v) for v in counts_)
    if swaps or flips or splits or moved:
        notes.append('{:,} swaps of edges and faces and collapses of nodes, {:,} flips of needle triangles of the surface, {:,} edges split and {:,} moves of nodes mended the poor tetrahedra'.format(swaps, flips, splits, moved))
    return [tuple(new_cells[4 * i:4 * i + 4]) for i in range(nt)]


_BOUNDARY_KEPT = [None]
_AROUND_KEPT = [None]


def _tets_key(tets):
    ''' What tells one list of tetrahedra from another, cheaply: its length and a hash of its content (the passes that move nodes ask again and again about the same tetrahedra; one that changes them -- a swap, a
        collapse -- gives another key, so nothing is kept for a list that was changed in place).  None when it cannot be hashed '''
    try:
        if isinstance(tets, array):
            return (len(tets), hash(tets.tobytes()))
        return (len(tets), hash(tuple(tuple(t) for t in tets) if tets and not isinstance(tets[0], tuple) else tuple(tets)))
    except TypeError:
        return None


def _tets_around(tets):
    ''' {node: the tetrahedra that have it}, kept for the last list of tetrahedra asked about (H211, the user, 2026-11-14, "A SPEED ISSUE IS AN ISSUE": it was made for every round of every pass, a second each on a mesh of
        183,000 cells).  The lists in it are the kept ones: the callers read them '''
    key = _tets_key(tets)
    kept = _AROUND_KEPT[0]
    if key is not None and kept is not None and kept[0] == key:
        return kept[1]
    around = {}
    for t in tets:
        for w in t:
            around.setdefault(w, []).append(t)
    if key is not None:
        _AROUND_KEPT[0] = (key, around)
    return around


def _boundary_edges(tets):
    ''' The edges of the faces that only one tetrahedron has, and those faces.  (The last answer is kept by the content of `tets`, see _tets_key: the passes that move nodes ask about the same tetrahedra eight times,
        and a dict of the sorted faces of every tetrahedron was most of a second each time on a mesh of 183,000 cells -- H211, the user, 2026-11-14, "A SPEED ISSUE IS AN ISSUE".  The caller gets copies) '''
    key = _tets_key(tets)
    kept = _BOUNDARY_KEPT[0]
    if key is not None and kept is not None and kept[0] == key:
        return set(kept[1]), list(kept[2])
    edges, faces = _boundary_edges_made(tets)
    if key is not None:
        _BOUNDARY_KEPT[0] = (key, edges, faces)
    return set(edges), list(faces)


def _boundary_edges_made(tets):
    count = {}
    for t in tets:
        a, b, c, d = t
        for f in ((a, c, b), (a, b, d), (a, d, c), (b, c, d)):
            k = tuple(sorted(f))
            if k in count:
                count[k] = None
            else:
                count[k] = f
    edges = set()
    faces = []
    for k, f in count.items():
        if f is None:
            continue
        faces.append(f)
        edges.add(_edge(f[0], f[1]))
        edges.add(_edge(f[1], f[2]))
        edges.add(_edge(f[2], f[0]))
    return edges, faces


def _to_tet10(P, tets, field, curved, planes=(), size=1.0):
    ''' 10-node tetrahedra: a node in the middle of every edge, on the surface when the edge is (and `curved`).  `planes` ([(axis, at)]): the planes the mesh was mirrored about (H226) -- the middle nodes that
        are put on the surface are put there on one side of them only and mirrored, the Newton steps on a field read in single precision would give a node and its image a hair of difference (0.03 mm, in places 0.3 mm, on the
        real bracket at 6 mm) '''
    boundary, _ = _boundary_edges(tets) if curved else (set(), None)
    mids = {}
    out = array('i')
    new_boundary = []
    for t in tets:
        row = list(t)
        for p, q in ((0, 1), (1, 2), (2, 0), (0, 3), (1, 3), (2, 3)):
            e = _edge(t[p], t[q])
            m = mids.get(e)
            if m is None:
                m = len(P) // 3
                mids[e] = m
                P.extend(((P[3 * e[0]] + P[3 * e[1]]) * 0.5, (P[3 * e[0] + 1] + P[3 * e[1] + 1]) * 0.5, (P[3 * e[0] + 2] + P[3 * e[1] + 2]) * 0.5))
                if e in boundary:
                    new_boundary.append((m, e))
            row.append(m)
        out.extend(row)
    if new_boundary:
        points = [(P[3 * m], P[3 * m + 1], P[3 * m + 2]) for m, _ in new_boundary]
        # (a midside node that moves so far that it folds the element is worse than a straight edge: a step is half an edge at most, so the node stays within a quarter of it)
        scales = [0.5 * math.sqrt((P[3 * e[0]] - P[3 * e[1]]) ** 2 + (P[3 * e[0] + 1] - P[3 * e[1] + 1]) ** 2 + (P[3 * e[0] + 2] - P[3 * e[1] + 2]) ** 2) for _, e in new_boundary]
        if planes:
            tol = 1e-6 * size
            grid = 1e-5 * size

            def canonical(p):
                ''' The point reflected about every plane that it is below (the side the mesh was fitted on is the upper one), and the axes it was reflected along '''
                r, along = list(p), []
                for a, at in planes:
                    if r[a] < at - tol:
                        r[a] = 2.0 * at - r[a]
                        along.append(a)
                return r, along

            def key(p):
                return tuple(int(round(c / grid)) for c in p)

            roots, found = {}, []
            for i, p in enumerate(points):
                c, along = canonical(p)
                found.append((c, along))
                if not along:
                    roots[key(c)] = i
            of = []                                           # (the middle node that stands for each one: itself when it is on the upper side)
            for i, (c, along) in enumerate(found):
                if not along:
                    of.append(i)
                    continue
                k0 = key(c)
                j = roots.get(k0)
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        for dz in (-1, 0, 1):
                            if j is None:
                                j = roots.get((k0[0] + dx, k0[1] + dy, k0[2] + dz))
                of.append(j if j is not None else i)          # (no image found: it is put on the surface itself)
            stand = sorted(set(of))
            moved = _snap_to_surface(field, [points[i] for i in stand], [scales[i] for i in stand], 2)
            at_root = {}
            for i, p in zip(stand, moved):
                p = list(p)
                for a, at in planes:
                    if abs(points[i][a] - at) <= tol:
                        p[a] = at                             # (a node on a plane stays on it)
                at_root[i] = p
            target = []
            for i, p in enumerate(points):
                root = of[i]
                q = list(at_root[root])
                if root != i:
                    for a in found[i][1]:
                        q[a] = 2.0 * dict(planes)[a] - q[a]
                target.append(q)
        else:
            target = _snap_to_surface(field, points, scales, 2)
        for (m, e), p in zip(new_boundary, target):
            P[3 * m], P[3 * m + 1], P[3 * m + 2] = p
    return out


def _compact_nodes(mesh):
    ''' The nodes that no cell uses taken out (a node that was collapsed into a neighbour, a vertex of the lattice outside the body) and the rest numbered again '''
    used = set()
    for conn in mesh.cells.values():
        used.update(conn)
    if len(used) == len(mesh.nodes) // 3:
        return
    order = sorted(used)
    renumber = {old: new for new, old in enumerate(order)}
    nodes = array('d')
    for old in order:
        nodes.extend((mesh.nodes[3 * old], mesh.nodes[3 * old + 1], mesh.nodes[3 * old + 2]))
    for kind in list(mesh.cells):
        mesh.cells[kind] = array('i', [renumber[v] for v in mesh.cells[kind]])
    mesh.nodes = nodes


def _boundary_of(mesh):
    ''' The faces that one cell only has, the way out: {kind: array of nodes}, in the cells' order '''
    seen = {}
    order = []
    for kind in SOLIDS:
        conn = mesh.cells.get(kind)
        if not conn:
            continue
        n = NODES[kind]
        k = CORNERS[kind]
        faces = FACES[kind]
        for i in range(len(conn) // n):
            ids = conn[n * i:n * i + k]
            for f in faces:
                nodes = tuple(ids[j] for j in f)
                key = tuple(sorted(nodes))
                if key in seen:
                    seen[key] = None
                else:
                    seen[key] = nodes
                    order.append(key)
    for faces in mesh.polyhedra():
        for nodes in faces:
            key = tuple(sorted(nodes))
            if key in seen:
                seen[key] = None
            else:
                seen[key] = nodes
                order.append(key)
    out = {'tri3': array('i'), 'quad4': array('i'), POLY: array('i')}
    sizes = array('i')
    for key in order:
        nodes = seen[key]
        if nodes is None:
            continue
        if len(nodes) == 3:
            out['tri3'].extend(nodes)
        elif len(nodes) == 4:
            out['quad4'].extend(nodes)
        else:
            out[POLY].extend(nodes)
            sizes.append(len(nodes))
    mesh.face_sizes = {POLY: sizes} if len(sizes) else {}
    return {k: v for k, v in out.items() if len(v)}


def _faces_of_kind(mesh, kind):
    ''' The boundary faces of one kind as tuples of nodes (the polygons of more than four nodes included) '''
    if kind == POLY:
        return list(mesh.polygons())
    n = NODES[kind]
    conn = mesh.faces[kind]
    return [tuple(conn[n * i:n * i + n]) for i in range(len(conn) // n)]


def _face_centres(mesh, kind):
    P = mesh.nodes
    return [_centroid(P, ids) for ids in _faces_of_kind(mesh, kind)]


def _face_normals(mesh, kind):
    P = mesh.nodes
    out = []
    for ids in _faces_of_kind(mesh, kind):
        ax, ay, az = _polygon_area_vector(P, ids)
        length = math.sqrt(ax * ax + ay * ay + az * az) or 1.0
        out.append((ax / length, ay / length, az / length, length))
    return out


def _assign_zones(mesh, zones, default, feature_angle, tolerance):
    ''' The zone of every boundary face: the first region of `zones` that has the middle of the face, else `default`; the faces of `default` split at the creases
        of `feature_angle` degrees, when it is given, into default_1, default_2 ... largest first '''
    regions = list((zones or {}).items())
    names = [n for n, _ in regions] + [default]
    for kind in list(mesh.faces):
        centres = _face_centres(mesh, kind)
        zone = [len(regions)] * len(centres)
        for index, (name, region) in enumerate(regions):
            if not isinstance(region, Shape) and callable(region):
                values = list(region(centres))
            else:
                values = _eval(region, centres)
            for i, v in enumerate(values):
                if zone[i] == len(regions) and v <= tolerance:
                    zone[i] = index
        mesh.face_zone[kind] = array('i', zone)
    mesh.zone_names = names
    if feature_angle:
        _split_walls(mesh, len(regions), default, feature_angle)
    # (a zone with no face is not listed)
    used = set()
    for kind in mesh.face_zone:
        used.update(mesh.face_zone[kind])
    keep = [i for i in range(len(mesh.zone_names)) if i in used]
    remap = {old: new for new, old in enumerate(keep)}
    for kind in mesh.face_zone:
        mesh.face_zone[kind] = array('i', [remap[v] for v in mesh.face_zone[kind]])
    mesh.zone_names = [mesh.zone_names[i] for i in keep]


def _split_walls(mesh, wall_index, default, angle):
    ''' The faces of the zone `wall_index` joined into pieces that are smooth: two faces that share an edge are one piece when the angle between their normals is
        less than `angle` '''
    cos_limit = math.cos(math.radians(angle))
    faces = []              # (kind, i, normal, area)
    edge_faces = {}
    for kind in list(mesh.faces):
        normals = _face_normals(mesh, kind)
        for i, ids in enumerate(_faces_of_kind(mesh, kind)):
            if mesh.face_zone[kind][i] != wall_index:
                continue
            fid = len(faces)
            faces.append((kind, i, normals[i][:3], normals[i][3]))
            n = len(ids)
            for k in range(n):
                edge_faces.setdefault(_edge(ids[k], ids[(k + 1) % n]), []).append(fid)
    parent = list(range(len(faces)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for fs in edge_faces.values():
        if len(fs) != 2:
            continue
        a, b = fs
        na, nb = faces[a][2], faces[b][2]
        if na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2] >= cos_limit:
            parent[find(a)] = find(b)
    # A thin strip of faces is no patch: where a lattice rounds an edge, or layers of prisms meet a plane, the faces make strips whose normals turn by any angle.  A piece
    # narrower than two edges of its own faces (its width is twice its area over its perimeter), or of fewer than four faces, joins the piece it shares most edge with.
    P = mesh.nodes
    length_of = {}
    for (u, v) in edge_faces:
        length_of[(u, v)] = math.sqrt(sum((P[3 * u + k] - P[3 * v + k]) ** 2 for k in range(3)))
    for _ in range(8):
        count = {}
        surface = {}
        perimeter = {}
        for fid, f in enumerate(faces):
            r = find(fid)
            count[r] = count.get(r, 0) + 1
            surface[r] = surface.get(r, 0.0) + f[3]
        if len(count) <= 1:
            break
        shared = {}
        for key, fs in edge_faces.items():
            roots = [find(x) for x in fs]
            if len(roots) == 2 and roots[0] == roots[1]:
                continue
            for r in set(roots):
                perimeter[r] = perimeter.get(r, 0.0) + length_of[key]
            if len(roots) == 2:
                shared.setdefault(roots[0], {})[roots[1]] = shared.get(roots[0], {}).get(roots[1], 0.0) + length_of[key]
                shared.setdefault(roots[1], {})[roots[0]] = shared.get(roots[1], {}).get(roots[0], 0.0) + length_of[key]
        thin = []
        for r in count:
            edge = math.sqrt(2.31 * surface[r] / count[r])
            width = 2.0 * surface[r] / perimeter[r] if perimeter.get(r) else 1e30
            if count[r] < 4 or width < 2.0 * edge:
                thin.append((width, r))
        merged = False
        for _, r in sorted(thin):
            options = shared.get(r)
            if not options or find(r) != r:
                continue
            target = find(max(options, key=options.get))
            if target != r:
                parent[r] = target
                merged = True
        if not merged:
            break
    area = {}
    for fid, f in enumerate(faces):
        r = find(fid)
        area[r] = area.get(r, 0.0) + f[3]
    if len(area) <= 1:
        return
    ranked = sorted(area, key=lambda r: -area[r])
    first_new = len(mesh.zone_names)
    for rank, r in enumerate(ranked):
        mesh.zone_names.append('{}_{}'.format(default, rank + 1))
    index = {r: first_new + rank for rank, r in enumerate(ranked)}
    for fid, f in enumerate(faces):
        mesh.face_zone[f[0]][f[1]] = index[find(fid)]


def _assign_cell_zones(mesh, cell_zones, tolerance):
    names = list(mesh.cell_zone_names)
    regions = list((cell_zones or {}).items())
    for name, _ in regions:
        names.append(name)
    for kind in SOLIDS:
        conn = mesh.cells.get(kind)
        if not conn:
            continue
        n = NODES[kind]
        k = CORNERS[kind]
        centres = [_centroid(mesh.nodes, conn[n * i:n * i + k]) for i in range(len(conn) // n)]
        zone = [0] * len(centres)
        for index, (name, region) in enumerate(regions):
            values = list(region(centres)) if not isinstance(region, Shape) and callable(region) else _eval(region, centres)
            for i, v in enumerate(values):
                if zone[i] == 0 and v <= tolerance:
                    zone[i] = index + 1
        mesh.cell_zone[kind] = array('i', zone)
    if mesh.cell_count(POLY):
        centres = [_poly_centroid(mesh.nodes, faces) for faces in mesh.polyhedra()]
        zone = [0] * len(centres)
        for index, (name, region) in enumerate(regions):
            values = list(region(centres)) if not isinstance(region, Shape) and callable(region) else _eval(region, centres)
            for i, v in enumerate(values):
                if zone[i] == 0 and v <= tolerance:
                    zone[i] = index + 1
        mesh.cell_zone[POLY] = array('i', zone)
    mesh.cell_zone_names = names


def _turn_matrix(angles):
    ''' The matrix of a placement's `rotation=(x, y, z)` in degrees: x first, then y, then z, Rz * Ry * Rx '''
    ax, ay, az = [math.radians(float(a)) for a in angles]
    cx, sx, cy, sy, cz, sz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    return ((cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx),
            (sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx),
            (-sy, cy * sx, cy * cx))


def _mesh_turned(body, axes, element_size, element, boundary_layers, zones, default_zone, feature_angle, cell_zones, curved, smooth, optimize, threshold, quanta, bounds, name):
    ''' A mesh whose grid of cubes is turned: the body is turned into the grid's frame (the grid of the cubes is along the axes), meshed there, and the nodes turned back.  The zones' regions are turned
        with the body, so that they still say where they said '''
    try:
        angles = tuple(float(a) for a in axes)
    except (TypeError, ValueError):
        angles = ()
    if len(angles) != 3:
        raise ValueError('mesh_tet4: axes is the turn of the grid in degrees, (x, y, z) as the rotation= of a placement')
    from fieldes.stdlib.cad import rotate
    R = _turn_matrix(angles)

    def into_frame(shape):
        # (a point q of the grid's frame is the point R q of the model: the body there is the body turned by the inverse, z first, then y, then x)
        return rotate(rotate(rotate(shape, (0, 0, 1), -angles[2]), (0, 1, 0), -angles[1]), (1, 0, 0), -angles[0])

    def region_into_frame(region):
        if isinstance(region, Shape):
            return into_frame(region)
        if callable(region):
            return lambda pts: region([tuple(R[i][0] * q[0] + R[i][1] * q[1] + R[i][2] * q[2] for i in range(3)) for q in pts])
        return region

    local_zones = {k: region_into_frame(v) for k, v in zones.items()} if zones else None
    local_cells = {k: region_into_frame(v) for k, v in cell_zones.items()} if cell_zones else None
    mesh = _mesh_any(into_frame(body), element_size, element, boundary_layers, local_zones, default_zone, feature_angle, local_cells, curved, smooth, optimize, threshold, quanta, bounds, name)
    P = mesh.nodes
    for i in range(len(P) // 3):
        x, y, z = P[3 * i], P[3 * i + 1], P[3 * i + 2]
        P[3 * i], P[3 * i + 1], P[3 * i + 2] = (R[0][0] * x + R[0][1] * y + R[0][2] * z, R[1][0] * x + R[1][1] * y + R[1][2] * z, R[2][0] * x + R[2][1] * y + R[2][2] * z)
    mesh.body = body
    mesh.notes = list(mesh.notes) + ['the grid turned by ({:.4g}, {:.4g}, {:.4g}) degrees: its cubes are along the sides of a body that is turned so'.format(*angles)]
    return mesh


def _mesh_any(body, element_size=None, element='tet', boundary_layers=None, zones=None, default_zone='wall', feature_angle=None, cell_zones=None,
              curved=True, smooth=None, optimize=True, threshold=4.0, quanta=0.5, bounds=None, name=None, axes=None, axis='z', layers=None):
    ''' The one mesher behind mesh_tet4, mesh_tet10, mesh_hex_quanta, mesh_hex20_quanta, mesh_cartesian and mesh_hex_extrusion: each of them takes only the arguments of its own way of meshing and calls this with the rest
        at their defaults.  The inside of `body` as a mesh of elements.

        element         'tet', 'tet10', 'hex_quanta', 'hex_quanta20', 'cartesian' or 'hex_extrusion': the kind of element and the way it is made (see the functions)
        the other arguments are the ones of those functions: element_size, boundary_layers, zones, default_zone, feature_angle, cell_zones, curved, smooth, optimize, threshold, quanta, bounds, name, axes, axis, layers '''
    t0 = time.time()
    if not isinstance(body, Shape):
        raise TypeError('mesh_tet4: the body is a Shape, not {!r}'.format(type(body).__name__))
    if element not in ELEMENTS:
        raise ValueError("mesh_tet4: element is " + ', '.join("'%s'" % e for e in ELEMENTS[:-1]) + " or '%s'" % ELEMENTS[-1])
    if element == 'hex_extrusion':
        # (H213: layers of hexahedra on a wall, as the tetrahedra have layers of prisms: the method meshes the CORE, the body with the layers taken off the wall, and the layers are the quadrilaterals of its
        # boundary swept to the wall -- see _add_hex_layers)
        inflation = None
        mesh_body, mesh_bounds, wall_distance = body, bounds, None
        if boundary_layers is not None:
            if not isinstance(boundary_layers, _Inflation):
                raise TypeError('mesh_tet4: boundary_layers is inflation_layers(...)')
            inflation = boundary_layers
            from fieldes.stdlib.csg import intersection
            from fieldes.stdlib.fields import _shape_bounds, tight_extent, exact_distance
            found = tight_extent(body) if bounds is None and not getattr(body, '_bounds', None) else None
            lo, hi = found if found is not None else _shape_bounds(body, bounds)
            thickness = inflation.thickness()
            wall_distance = exact_distance(inflation.on, reach=max(2.0 * thickness, 4.0 * inflation.first))
            mesh_body = intersection(body, thickness - wall_distance)
            mesh_bounds = (tuple(float(c) for c in lo), tuple(float(c) for c in hi))
        mesh_zones = zones if inflation is None else None
        mesh = _mesh_extrusion(mesh_body, element_size, axis, layers, 10 if smooth is None else smooth, mesh_zones, default_zone, feature_angle, mesh_bounds, name)
        if inflation is not None:
            mesh.body = body
            notes = list(getattr(mesh, 'notes', None) or [])
            notes.append('{} layers on the wall, {:.4g} mm thick in all ({:.4g} mm at the wall, growing by {:.4g}); the core is the body with them taken off'.format(
                inflation.layers, thickness, inflation.first, inflation.growth))
            _add_hex_layers(mesh, wall_distance, inflation, notes)
            mesh.notes = notes
            mesh.faces = _boundary_of(mesh)
            mesh.face_zone = {}
            _assign_zones(mesh, zones, default_zone, feature_angle, 0.02 * (mesh.element_size or 1.0))
        return mesh
    smooth = 6 if smooth is None else smooth
    if axes is not None:
        return _mesh_turned(body, axes, element_size, element, boundary_layers, zones, default_zone, feature_angle, cell_zones, curved, smooth, optimize, threshold, quanta, bounds, name)
    from fieldes.stdlib.fields import _shape_bounds, tight_extent
    # (the box the mesh is fitted to: the body's own faces where they are flat -- see tight_extent; H202)
    found = tight_extent(body) if bounds is None and not getattr(body, '_bounds', None) else None
    lo, hi = found if found is not None else _shape_bounds(body, bounds)
    extent = max(float(hi[i]) - float(lo[i]) for i in range(3))
    if not extent > 0:
        raise MeshError('mesh_tet4: the body has no size')
    pad = 0.0       # (the lattice of the tetrahedra and the grid of the hexahedra are fitted to this box: the faces of the body on theirs exactly, not a millionth off them -- measured on the 10-node plate with a hole: with a millionth of padding the stress concentration was -1.2, +15.1 and +28.8 % off Peterson's at element sizes 0.65, 0.70 and 0.75, without it +1.0, -0.7 and +5.5 %; H202)
    box = (tuple(float(c) - pad for c in lo), tuple(float(c) + pad for c in hi))
    notes = []
    layers = None
    if boundary_layers is not None:
        if not isinstance(boundary_layers, _Inflation):
            raise TypeError('mesh_tet4: boundary_layers is inflation_layers(...)')
        layers = boundary_layers
    hexahedral = element in ('hex_quanta', 'hex_quanta20', 'cartesian')
    if element_size is None:
        # (the tetrahedra's default size also holds the thin walls of the body, H193: see thin_size; the grids of cubes are made on a size of their own)
        element_size = default_element_size(body, box[0], box[1], 40.0, DEFAULT_ELEMENTS, thin=not hexahedral, notes=notes, quadratic=element == 'tet10')
        if hexahedral:
            # (a grid of cubes: the budget is cells, not tetrahedra -- about a third of the tetrahedra's number holds the same body)
            from fieldes.stdlib.element_size import estimated_volume
            budget = DEFAULT_HEXAHEDRA
            if element == 'cartesian':
                # (H223, his words of 2026-11-16: "TET4 IS REALLY GOOD", measured on the real bracket at the same nominal size of 3.75 mm, the same code for both: the Cartesian mesh had 4,630 skin faces against 15,584 of the tetrahedra,
                # 5.2 % of them leaning more than 12 degrees against 1.1 %, 18.2 % with their middle off the true surface against 5.4 %.  The same number is not the same size: it is the EDGE of a tetrahedron, whose skin
                # triangle has 0.43 of the area of the face of a cube of that edge, and the faces of a curved surface are wrong by about the square of their size.  The cube of the default size has the edge that gives
                # a skin face of the area of the tetrahedra's: 0.66 of the size.  The budget of cells is 40,000 for it)
                element_size *= CARTESIAN_DEFAULT_FACTOR
                budget = CARTESIAN_DEFAULT_CELLS
                notes.append('the default element size of mesh_cartesian is {:.2g} of the size of the tetrahedra (a cube of {:.4g} mm: its skin faces are as large as the skin triangles of mesh_tet4 at its default size, {:.4g} mm)'.format(
                    CARTESIAN_DEFAULT_FACTOR, element_size, element_size / CARTESIAN_DEFAULT_FACTOR))
            volume = estimated_volume(body, box[0], box[1])
            if volume and volume > 0.0 and volume / element_size ** 3 > budget:
                element_size = (volume / budget) ** (1.0 / 3.0)
            # (H193: a grid of cubes loses a wall thinner than about its cube as the lattice of tetrahedra does -- a tube with a wall of 0.6 mm and a length of 40 was meshed at 1 mm and held 22 % of its volume with hexahedra and 61 % with
            # the cartesian cells --: the size that holds the walls, within the budget of cells as far as that holds them and never more than four times it)
            element_size, note = thin_size(body, box[0], box[1], element_size, budget, 2.0, 4 * budget, None, None, 'hexahedra')
            if note:
                notes.append(note)
    sizer = _Sizer(element_size)
    base = sizer.coarsest(body, box[0], box[1])
    if sizer.uniform is None and base > extent:
        base = extent / 2.0
    core = body
    wall_distance = None
    if layers is not None:
        thickness = layers.thickness()
        from fieldes.stdlib.csg import intersection
        from fieldes.stdlib.fields import exact_distance
        wall_distance = exact_distance(layers.on, reach=max(2.0 * thickness, 4.0 * layers.first))
        core = intersection(body, thickness - wall_distance)
        notes.append('{} layers on the wall, {:.4g} mm thick in all ({:.4g} mm at the wall, growing by {:.4g})'.format(layers.layers, thickness, layers.first, layers.growth))
    if element == 'cartesian':
        mesh = _mesh_cartesian_cells(body, core, box, sizer, threshold, layers, zones, default_zone, feature_angle, cell_zones, notes, name, t0)
        mesh.box = box
        return mesh
    if hexahedral:
        mesh = _mesh_hex_cells(body, core, box, sizer, element, quanta, layers, zones, default_zone, feature_angle, cell_zones, notes, name, t0)
        mesh.box = box
        return mesh
    P, T = _kernel_tets(core, box[0], box[1], base, snap=sizer.uniform is not None, symmetric_mend=(1 if optimize else 0) if sizer.uniform is not None else None)
    mirrored = _MIRRORED[0] if sizer.uniform is not None else 0
    if not len(T):
        raise MeshError('mesh_tet4: the body has nothing inside the box it is meshed in')
    if mirrored:
        notes.append('the body is symmetric about {} plane{} through the middle of its box: meshed as the mirror image of a half{}, so the mesh is symmetric too'.format(
            mirrored, '' if mirrored == 1 else 's', ' that was mended before it was mirrored' if optimize else ''))
    if sizer.uniform is None:
        tet_list = _refine_to_size(P, T, sizer, core, notes=notes)
    else:
        tet_list = [tuple(T[4 * i:4 * i + 4]) for i in range(len(T) // 4)]
    if sizer.uniform is None:
        _fit_surface(P, tet_list, core, notes)
        tet_list = _split_creases(P, tet_list, core, notes, sizer)
    # (the edges, corners and rims are put right by the kernel (H223): in the lattice mesh that libfive_tetmesh_new makes, as in an analysis -- one mesher; a graded mesh was refined after that, so the kernel does it again, with the poorer floor for the
    # corners where its layers meet the walls: the ledge in the ring of prisms of example 36 was 0.095 mm with 0.3, 0.004 with 0.1, skewness 0.889 as before)
    if sizer.uniform is None:
        tet_list = _snap(P, tet_list, core, notes, 0.1)
    if mirrored:
        pass                    # (H226: the half was mended before it was mirrored; a pass over the whole mesh goes through it in an order that a mirror reverses, and the mesh would not be symmetric any more)
    elif optimize:
        tet_list = _mend(P, tet_list, core, smooth, notes)
        tet_list = _lift_caps(P, tet_list, notes)
    elif smooth and sizer.uniform is None:
        tet_list = _mend(P, tet_list, None, smooth, notes, rounds=0)
    _BOUNDARY_KEPT[0] = _AROUND_KEPT[0] = None          # (what the passes kept of the last tetrahedra is not kept after them: it is as big as the mesh)
    T = array('i')
    for t in tet_list:
        T.extend(t)
    mesh = Mesh(name or 'mesh')
    mesh.element = element
    mesh.element_size = sizer.uniform if sizer.uniform is not None else base
    mesh.graded = sizer.uniform is None             # (made with a size that varies on purpose: its spread of sizes is its design, see MeshQuality.spread)
    mesh.mirrored = mirrored                        # (H226: the planes the mesh was mirrored about: a skin that is remeshed afterwards is remeshed on one side of them too)
    mesh.notes = notes
    mesh.body = body
    mesh.box = box
    mesh.nodes = P
    if element == 'tet':
        mesh.cells['tet4'] = T
        if layers is not None:
            _add_layers(mesh, tet_list or [tuple(T[4 * i:4 * i + 4]) for i in range(len(T) // 4)], wall_distance, layers, notes)
    else:
        tet_list = tet_list or [tuple(T[4 * i:4 * i + 4]) for i in range(len(T) // 4)]
        mesh.cells['tet10'] = _to_tet10(P, tet_list, core, curved, _mirror_planes(core, box[0], box[1], base) if mirrored and curved else (), base)
    _compact_nodes(mesh)
    mesh.faces = _boundary_of(mesh)
    tolerance = 0.02 * (mesh.element_size or base)
    _assign_zones(mesh, zones, default_zone, feature_angle, tolerance)
    _assign_cell_zones(mesh, cell_zones, tolerance)
    mesh.seconds = time.time() - t0
    return mesh


def _mirror_planes(field, lo, hi, h):
    ''' The planes through the middle of the box [lo, hi] that `field` is symmetric about and that the symmetry argument allows (sim_mesh.current_symmetry), as [(axis, at)]: the decision is the kernel's, the one that
        meshShape makes for the tetrahedra (H226); a plane whose half is less than two cells across is left out, as the kernel leaves it out '''
    from fieldes.stdlib import sim_mesh
    mask = sim_mesh.current_symmetry()
    fn = getattr(lib, 'libfive_tetmesh_find_mirrors', None)
    if not mask or fn is None:
        return []
    _need_kernel()
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = float(lo[i]), float(hi[i])
    axes = (ctypes.c_int * 3)()
    places = (ctypes.c_double * 3)()
    count = int(fn(field.ptr, region, float(h), int(mask), axes, places))
    return [(int(axes[i]), float(places[i])) for i in range(count) if float(hi[int(axes[i])]) - float(places[i]) >= 2.0 * float(h)]


def _aligned_note(fit):
    ''' What the grid did with the flat faces of the body (H223), as a note: '' when no plane of it was put on one '''
    aligned = getattr(fit, 'aligned', None)
    if not aligned or not any(aligned):
        return ''
    narrow, wide = fit.width_range
    return 'the planes of the grid were put on {} flat faces of the body that stand square to an axis ({} along x, {} along y, {} along z), so the cells are bricks {:.2g} to {:.2g} of the element size across (align=False: equal cubes)'.format(
        sum(len(a) for a in aligned), len(aligned[0]), len(aligned[1]), len(aligned[2]), narrow / fit.size, wide / fit.size)


def _fit_cells(core, box, size, **options):
    ''' The Fit (bodyfit.py) of a grid of cubes cut by `core`: the mirror image of a fit of one side when the body is symmetric about planes through the middle of its box (H226), else a fit of the whole body '''
    from fieldes.stdlib import bodyfit
    options.setdefault('align', _ALIGN[0])
    planes = _mirror_planes(core, box[0], box[1], size)
    if planes and not options['align']:
        # (a grid of equal cubes has a plane of the grid at the middle of the box only when it has an even number of cubes along that axis; with an odd number the middle cube straddles the plane, and the half grid -- whole
        # cubes from the plane to the end -- would make cubes of another size: 5 cubes of 2 mm across a box of 10 became 2 of 2.5.  Such an axis is not mirrored: the grid is symmetric about the plane by itself)
        planes = [(axis, at) for axis, at in planes
                  if max(2, int(round((float(box[1][axis]) - float(box[0][axis])) / float(size)))) % 2 == 0 and int(round((float(box[1][axis]) - float(box[0][axis])) / float(size))) >= 4]
    if planes:
        return bodyfit.MirroredFit(core, box[0], box[1], size, planes, **options)
    return bodyfit.Fit(core, box[0], box[1], size, **options)


def _mesh_hex_cells(body, core, box, sizer, element, quanta, layers, zones, default_zone, feature_angle, cell_zones, notes, name, t0):
    ''' Hexahedra only ('hex_quanta'), or with a node in the middle of every edge ('hex_quanta20'): the cubes of a Cartesian grid that lie in the body, and the cubes that the surface cuts when `quanta` of them is
        inside -- Salome's Body Fitting with Quanta (see bodyfit.py).  The boundary is a staircase of whole cubes; every cell is a perfect cube.  (The method of this element before H193 -- cubes whose skin was
        pushed onto the surface and patched where it came out flat -- is gone: the user, 2026-11-14, "This SHOULD NOT BE IN OUR PROGRAM!!") '''
    from fieldes.stdlib import bodyfit
    if sizer.uniform is None:
        raise MeshError("mesh_tet4: a grid of cubes is of one size: give element_size as a number (a size field and graded_size are for tetrahedra)")
    if not 0.0 < float(quanta) <= 1.0:
        raise ValueError('mesh_tet4: quanta is the part of a cube that must be inside the body for the cube to be made, above 0 and at most 1')
    fit = _fit_cells(core, box, sizer.uniform, quanta=quanta)
    cells = fit.run()
    if not cells:
        raise MeshError('mesh_tet4: no cube of the grid holds the body: use a smaller element_size')
    if isinstance(fit, bodyfit.MirroredFit):
        notes.append('the body is symmetric about {} plane{} through the middle of its box: meshed on one side and mirrored, so the mesh is symmetric too'.format(len(fit.planes), '' if len(fit.planes) == 1 else 's'))
    mesh = Mesh(name or 'mesh')
    mesh.element = element
    mesh.element_size = sizer.uniform
    mesh.body = body
    nodes = array('d')
    for p in fit.pos:
        nodes.extend(p)
    mesh.nodes = nodes
    H = array('i')
    for _, ids in cells:
        H.extend(ids)
    if element == 'hex_quanta20':
        mesh.cells['hex20'] = _midsides(nodes, H)
    else:
        mesh.cells['hex8'] = H
    _compact_nodes(mesh)
    mesh.faces = _boundary_of(mesh)
    # (the faces of the staircase lie up to a cube's diagonal from the surface: a region of the body holds a face whose middle is within that of it)
    tolerance = 0.75 * sizer.uniform
    _assign_zones(mesh, zones, default_zone, feature_angle, tolerance)
    _assign_cell_zones(mesh, cell_zones, tolerance)
    cut = len(cells) - fit.whole
    notes.append('a grid of cubes of {:.4g} mm, {:,} hexahedra: {:,} lie in the body and {:,} are cubes the surface cuts with at least {:.3g} of the cube inside; {:,} cut cubes with less inside are left out, so the boundary '
                 'is a staircase of whole cubes'.format(fit.d[0], len(cells), fit.whole, cut, float(quanta), fit.dropped))
    if _aligned_note(fit):
        notes.append(_aligned_note(fit))
    mesh.notes = notes
    mesh.seconds = time.time() - t0
    return mesh


def _midsides(P, H):
    ''' The twelve mid-side nodes of every hexahedron (the 20-node element): twenty nodes to a cell, the eight corners and then the middle of the edges 0-1, 1-2, 2-3, 3-0, 4-5, 5-6, 6-7, 7-4, 0-4,
        1-5, 2-6, 3-7 (the nodes are appended to P).  The edges of a cube of the grid are straight, so the middle node is the middle of the edge '''
    edge_node = {}
    out = array('i')
    for c in range(len(H) // 8):
        ids = H[8 * c:8 * c + 8]
        row = list(ids)
        for a, b in EDGES['hex8']:
            x, y = ids[a], ids[b]
            e = (x, y) if x < y else (y, x)
            m = edge_node.get(e)
            if m is None:
                m = len(P) // 3
                edge_node[e] = m
                P.extend(((P[3 * x] + P[3 * y]) * 0.5, (P[3 * x + 1] + P[3 * y + 1]) * 0.5, (P[3 * x + 2] + P[3 * y + 2]) * 0.5))
            row.append(m)
        out.extend(row)
    return out


def _mesh_cartesian_cells(body, core, box, sizer, threshold, layers, zones, default_zone, feature_angle, cell_zones, notes, name, t0):
    ''' A Cartesian grid cut by the body (see bodyfit.py): hexahedra inside, the polyhedra of what is inside the cubes that the surface cuts '''
    from fieldes.stdlib import bodyfit
    if sizer.uniform is None:
        raise MeshError("mesh_tet4: a grid of cubes is of one size: give element_size as a number (a size field and graded_size are for tetrahedra)")
    fit = _fit_cells(core, box, sizer.uniform, threshold=threshold)
    cells = fit.run()
    if not cells:
        raise MeshError('mesh_tet4: no cube of the grid holds the body: use a smaller element_size')
    if isinstance(fit, bodyfit.MirroredFit):
        notes.append('the body is symmetric about {} plane{} through the middle of its box: meshed on one side and mirrored, so the mesh is symmetric too'.format(len(fit.planes), '' if len(fit.planes) == 1 else 's'))
    mesh = Mesh(name or 'mesh')
    mesh.element = 'cartesian'
    mesh.element_size = sizer.uniform
    mesh.body = body
    nodes = array('d')
    for p in fit.pos:
        nodes.extend(p)
    mesh.nodes = nodes
    fixed = {}
    for kind, ids in cells:
        if kind == POLY:
            mesh.poly_faces_of.append(len(ids))
            for fc in ids:
                mesh.poly_sizes.append(len(fc))
                mesh.cells.setdefault(POLY, array('i')).extend(fc)
        else:
            mesh.cells.setdefault(kind, array('i')).extend(ids)
            fixed[kind] = fixed.get(kind, 0) + 1
    _compact_nodes(mesh)
    mesh.faces = _boundary_of(mesh)
    tolerance = 0.02 * sizer.uniform
    _assign_zones(mesh, zones, default_zone, feature_angle, tolerance)
    _assign_cell_zones(mesh, cell_zones, tolerance)
    shares = ', '.join('{:,} {}'.format(mesh.cell_count(k), k) for k in SOLIDS + (POLY,) if mesh.cell_count(k))
    notes.append('a grid of cubes of {:.4g} mm cut by the body: {}; {:,} cut cells with at most 1/{:.4g} of a cube inside were joined to the cell they share the most face with{}{}'.format(
        fit.d[0], shares, fit.merged, threshold, '' if not fit.dropped else ' ({:,} without a neighbour or flat were left out)'.format(fit.dropped),
        '' if not (fit.edge_moved or fit.collapsed) else '; {:,} nodes were put on an edge of the body and {:,} taken into a node beside them'.format(fit.edge_moved, fit.collapsed)))
    if _aligned_note(fit):
        notes.append(_aligned_note(fit))
    mesh.notes = notes
    mesh.seconds = time.time() - t0
    return mesh


def _run(function, body, element_size, element, **options):
    ''' Calls the one mesher for one of the public functions; what it says is said in the name of that function (not in mesh_tet4's) '''
    try:
        return _mesh_any(body, element_size, element, **options)
    except (ValueError, TypeError, MeshError) as error:
        text = str(error)
        if function != 'mesh_tet4' and 'mesh_tet4' in text:
            raise type(error)(text.replace('mesh_tet4', function)) from None
        raise


def mesh_tet4(body, element_size=None, boundary_layers=None, zones=None, default_zone='wall', feature_angle=None, cell_zones=None, smooth=None, optimize=True, bounds=None, name=None, surface_fit=0.5, symmetry='auto'):
    ''' The inside of `body` as a mesh of TETRAHEDRA of 4 nodes that follow its surface (see the description of this module).

            mesh = mesh_tet4(part, element_size=2.0)

        element_size    the element edge in mm, or a field (see graded_size); default: about 40 elements along the longest side, no more than about a hundred thousand tetrahedra
        boundary_layers inflation_layers(...): layers of prisms on the surface of a solid, growing into the mesh (Salome's Viscous Layers)
        zones           {name: region}: the patches of the boundary; the rest are `default_zone`; feature_angle (degrees) splits those at the creases
        cell_zones      {name: region}: zones of cells; the rest are 'fluid'
        smooth          rounds of moving the nodes of poor tetrahedra toward the middle of their neighbours (default 6); 0 for none
        optimize        mend the poor tetrahedra: twice the nodes are moved as `smooth` says and edges and faces are swapped (False: the mesh as it comes, the fastest)
        bounds          ((x0, y0, z0), (x1, y1, z1)): the box to mesh in, when the body's cannot be found
        surface_fit     a number from 0 to 1 that says what the mesh gives up where the exact surface and good elements pull against each other (a sharp edge that the lattice cuts across):
                        0.5 by default (what the mesher has always done); towards 1 nodes go onto edges even where the elements get poorer and the skin stays nearer the exact surface; towards 0
                        a move that would leave a poor element is refused and the skin may stand further off the surface to remove a sliver.  The limits each number stands for are in
                        docs/analysis.md, Surface or elements
        symmetry        'auto' (default), None, or the axes to look at ('x', 'y', 'z', 'xz'): a body that is symmetric about a plane through the middle of its box is meshed as the mirror image of a
                        half that was mended before it was mirrored, so that the mesh is symmetric too (a lattice cut by a body cannot be); None meshes the body whole.  A graded mesh (a size field) and
                        a mesh with layers are meshed whole '''
    from fieldes.stdlib import sim_mesh
    fit = sim_mesh.surface_fit_value(surface_fit, 'mesh_tet4')
    sym = sim_mesh.symmetry_value(symmetry, 'mesh_tet4')
    with sim_mesh.surface_fit_scope(fit, sym):
        return _run('mesh_tet4', body, element_size, 'tet', boundary_layers=boundary_layers, zones=zones, default_zone=default_zone, feature_angle=feature_angle, cell_zones=cell_zones,
                    smooth=smooth, optimize=optimize, bounds=bounds, name=name)


def mesh_tet10(body, element_size=None, zones=None, default_zone='wall', feature_angle=None, cell_zones=None, curved=True, smooth=None, optimize=True, bounds=None, name=None, surface_fit=0.5, symmetry='auto'):
    ''' The inside of `body` as a mesh of TETRAHEDRA of 10 nodes: the tetrahedra of mesh_tet4 with a node in the middle of every edge, the ones on the surface put on it (the edges there curved).

            mesh = mesh_tet10(part, element_size=3.0)

        element_size    the element edge in mm, or a field (see graded_size); default: about 40 elements along the longest side, no more than about a hundred thousand tetrahedra
        zones           {name: region}: the patches of the boundary; the rest are `default_zone`; feature_angle (degrees) splits those at the creases
        cell_zones      {name: region}: zones of cells; the rest are 'fluid'
        curved          the nodes in the middle of the edges and faces on the boundary lie on the surface (False: halfway between the corners)
        smooth, optimize, bounds, name, surface_fit, symmetry   as for mesh_tet4

        Layers of prisms (boundary_layers) are made with mesh_tet4: a prism has no node in the middle of every edge of the tetrahedra. '''
    from fieldes.stdlib import sim_mesh
    fit = sim_mesh.surface_fit_value(surface_fit, 'mesh_tet10')
    sym = sim_mesh.symmetry_value(symmetry, 'mesh_tet10')
    with sim_mesh.surface_fit_scope(fit, sym):
        return _run('mesh_tet10', body, element_size, 'tet10', zones=zones, default_zone=default_zone, feature_angle=feature_angle, cell_zones=cell_zones, curved=curved,
                    smooth=smooth, optimize=optimize, bounds=bounds, name=name)


_ALIGN = [True]             # (H223: whether the grid of the cubes of the next mesh has its planes put on the flat faces of the body: the argument `align` of the grid meshers, read by _fit_cells)


def _symmetric_run(function, symmetry, *args, align=True, **kwargs):
    ''' _run inside the scope of the symmetry argument (H226): the meshers that have no surface_fit keep the one that stands; and with the choice `align` of the grid meshers (H223) '''
    from fieldes.stdlib import sim_mesh
    mask = sim_mesh.symmetry_value(symmetry, function)
    if not isinstance(align, bool):
        raise ValueError('{}: align is True (the planes of the grid are put on the flat faces of the body, the default) or False (a grid of equal cubes), not {!r}'.format(function, align))
    before = _ALIGN[0]
    _ALIGN[0] = align
    try:
        with sim_mesh.surface_fit_scope(sim_mesh.current_surface_fit(), mask):
            return _run(function, *args, **kwargs)
    finally:
        _ALIGN[0] = before


def mesh_hex_quanta(body, element_size=None, quanta=0.5, axes=None, zones=None, default_zone='wall', feature_angle=None, cell_zones=None, bounds=None, name=None, symmetry='auto', align=False):
    ''' The inside of `body` as HEXAHEDRA of 8 nodes only: the cubes of a Cartesian grid, those the surface cuts included when `quanta` of them is inside (Salome's Body Fitting with Quanta).  Every cell is
        a perfect cube; the boundary is a staircase of whole cubes.

            mesh = mesh_hex_quanta(part, 2.0)

        element_size    the edge of the cubes in mm; default: about fifteen thousand cubes
        quanta          a cube that the surface cuts is a whole cube of the grid when what is inside holds at least this part of it (0.5 by default, so that the mesh has about the volume of the body), and is left out when it holds less
        axes            the grid of cubes turned, as the rotation= of a placement is -- (x, y, z) in degrees: a box turned by 30 degrees about z is a block of perfect cubes along its own sides with axes=(0, 0, 30)
        zones, default_zone, feature_angle, cell_zones, bounds, name   as for mesh_tet4
        symmetry        'auto' (default), None, or the axes to look at ('x', 'y', 'z', 'xz'): a body that is symmetric about a plane through the middle of its box is meshed on one side of it and
                        mirrored, so the mesh is exactly symmetric; None meshes the body whole
        align           False (default): a grid of equal cubes, every cell a perfect cube.  True: the planes of the grid are put ON the flat faces of the body that stand square to an axis, so a crease between two of
                        them is an edge of the grid and the boundary is exact there; the cells are then bricks, 0.6 to 1.5 of the element size across, where the gap between two such faces is not a whole number of cubes

        The grid has one size: no size field and no layers.  The cubes with a node in the middle of every edge are mesh_hex20_quanta. '''
    return _symmetric_run('mesh_hex_quanta', symmetry, body, element_size, 'hex_quanta', zones=zones, default_zone=default_zone, feature_angle=feature_angle,
                          cell_zones=cell_zones, quanta=quanta, bounds=bounds, name=name, axes=axes, align=align)


def mesh_hex20_quanta(body, element_size=None, quanta=0.5, axes=None, zones=None, default_zone='wall', feature_angle=None, cell_zones=None, bounds=None, name=None, symmetry='auto', align=False):
    ''' The inside of `body` as HEXAHEDRA of 20 nodes only: the cubes of mesh_hex_quanta with a node in the middle of every edge.

            mesh = mesh_hex20_quanta(part, 2.0)

        All the arguments are the ones of mesh_hex_quanta. '''
    return _symmetric_run('mesh_hex20_quanta', symmetry, body, element_size, 'hex_quanta20', zones=zones, default_zone=default_zone, feature_angle=feature_angle,
                          cell_zones=cell_zones, quanta=quanta, bounds=bounds, name=name, axes=axes, align=align)


def mesh_cartesian(body, element_size=None, threshold=4.0, axes=None, zones=None, default_zone='wall', feature_angle=None, cell_zones=None, bounds=None, name=None, symmetry='auto', align=True):
    ''' The inside of `body` as a grid of cubes CUT by the body: hexahedra inside it, and where the surface cuts a cube the cell of what is inside -- a tetrahedron, prism, pyramid, hexahedron or polyhedron by its faces --
        with its nodes on the surface (Salome's Body Fitting).

            mesh = mesh_cartesian(part, 2.0)

        element_size    the edge of the cubes in mm; default: about fifteen thousand cubes
        threshold       a cut cell that holds no more than 1 / threshold of a cube (4 by default, a quarter, as Salome's; 10, a tenth, joins fewer) is JOINED to the cell it shares the most face with, a whole cube or another
                        cut cell, so that no cell is smaller; its polygon of the surface stays the boundary.  (Salome leaves such a cell out and the faces of its neighbours become the boundary: a staircase up to 0.65 of a
                        cube off the surface, with faces that lean up to 45 degrees from it.)  4 makes no cell smaller than a quarter of a cube and the volume ratio of neighbours at most 5, at the price of more joined cells,
                        which are polyhedra that are not convex (the worst orthogonal quality of a plate with a hole 0.50 where 10 gives 0.89); 1e9 keeps every cut cell as a cell of its own.  A cut cell that is a wedge
                        (scaled Jacobian under 0.2) is joined whatever it holds.  The nodes the snap took for on the surface (within a tenth of a cube of it) are put on it
        axes            the grid turned, as for mesh_hex_quanta
        zones, default_zone, feature_angle, cell_zones, bounds, name   as for mesh_tet4
        symmetry        as for mesh_hex_quanta: a symmetric body is meshed on one side of the plane and mirrored
        align           as for mesh_hex_quanta: the planes of the grid are put on the flat faces of the body that stand square to an axis (True, the default), so that the edges between them are exact and no cell is cut
                        across them -- where the cut cells of a grid of equal cubes have a bevel; False: a grid of equal cubes

        The grid has one size: no size field and no layers. '''
    return _symmetric_run('mesh_cartesian', symmetry, body, element_size, 'cartesian', zones=zones, default_zone=default_zone, feature_angle=feature_angle, cell_zones=cell_zones,
                          threshold=threshold, bounds=bounds, name=name, axes=axes, align=align)


def mesh_hex_extrusion(body, element_size=None, axis='z', layers=None, smooth=None, boundary_layers=None, zones=None, default_zone='wall', feature_angle=None, bounds=None, name=None, symmetry='auto'):
    ''' A body that is a PRISM along `axis` as hexahedra: its end face meshed with quadrilaterals and swept along it, the surface exact (Salome's Extrusion 3D).  A body that is not a prism along the axis is refused.

            mesh = mesh_hex_extrusion(plate_with_holes, 1.0)             # along z, the cells about 1 mm across

        element_size    the cell edge in mm; default: about fifteen thousand cells
        axis            the axis the body is a prism along: 'x', 'y' or 'z'
        layers          the layers along it: a whole number, or a list of their thicknesses in mm (default: cells of the element size)
        smooth          rounds of smoothing the hexahedra (default 10)
        boundary_layers inflation_layers(...): layers of hexahedra on the surface of a solid, growing into the mesh
        zones, default_zone, feature_angle, bounds, name   as for mesh_tet4
        symmetry        'auto' (default), None, or the axes to look at ('x', 'y', 'z', 'xz'): a body that is symmetric about a plane along the axis of the prism has its end face made on one side of the
                        plane and mirrored, so the mesh is exactly symmetric; None meshes the end face whole '''
    return _symmetric_run('mesh_hex_extrusion', symmetry, body, element_size, 'hex_extrusion', boundary_layers=boundary_layers, zones=zones, default_zone=default_zone, feature_angle=feature_angle,
                          smooth=smooth, bounds=bounds, name=name, axis=axis, layers=layers)


def _surface(function, body, element_size, quads, zones, default_zone, feature_angle, bounds, name):
    ''' The surface of a body as the boundary of the tetrahedra that mesh_tet4 makes: triangles, or quadrilaterals (every triangle cut into three) '''
    mesh = _run(function, body, element_size, 'tet', zones=zones, default_zone=default_zone, feature_angle=feature_angle, bounds=bounds, name=name or 'surface')
    out = mesh.surface_mesh()
    size = float(out.element_size or 0.0)
    if not zones and size > 0.0 and out.body is not None:
        # (H223: the skin of the lattice mesh is made regular -- near-equilateral triangles of the size asked, as Salome's surface mesher gives -- see surface_remesh.py; a mesh with zones of its own keeps its skin)
        try:
            from fieldes.stdlib import surface_remesh
            before = out.face_count('tri3')
            # (H226: a skin that is symmetric is remeshed on one side of the plane and mirrored, so that it stays symmetric; the planes are the kernel's decision, the one the volume mesh was made with)
            planes = _mirror_planes(mesh.body, mesh.box[0], mesh.box[1], size) if getattr(mesh, 'box', None) is not None and getattr(mesh, 'mirrored', 0) else []
            if surface_remesh.remesh_skin(out, size, 30.0 if feature_angle is None else float(feature_angle), planes):
                out.notes = list(out.notes) + ['the surface remeshed to triangles of about {:.3g} mm: {:,} triangles (the skin of the volume mesh had {:,})'.format(size, out.face_count('tri3'), before)]
        except Exception as e:                    # (a surface that cannot be remeshed keeps the skin it has)
            out.notes = list(out.notes) + ['the surface was not remeshed: {}'.format(e)]
    if quads:
        _tris_to_quads(out)
    out.element = 'quad' if quads else 'tri'
    return out


def mesh_surface_tri(body, element_size=None, zones=None, default_zone='wall', feature_angle=None, bounds=None, name=None, symmetry='auto'):
    ''' The surface of `body` as a mesh of TRIANGLES.  It is the boundary of the tetrahedra that mesh_tet4 makes, so it follows the body's surface and is conforming, and it costs the volume mesh; its arguments are
        mesh_tet4's.

            surface = mesh_surface_tri(part, 1.0)

        symmetry        'auto' (default), None, or the axes to look at ('x', 'y', 'z', 'xz'): a body that is symmetric about a plane through the middle of its box has a symmetric surface mesh (the half is
                        remeshed and mirrored); None meshes the body whole

        The surface as quadrilaterals is mesh_surface_quad. '''
    from fieldes.stdlib import sim_mesh
    mask = sim_mesh.symmetry_value(symmetry, 'mesh_surface_tri')
    with sim_mesh.surface_fit_scope(sim_mesh.current_surface_fit(), mask):
        return _surface('mesh_surface_tri', body, element_size, False, zones, default_zone, feature_angle, bounds, name)


def mesh_surface_quad(body, element_size=None, zones=None, default_zone='wall', feature_angle=None, bounds=None, name=None, symmetry='auto'):
    ''' The surface of `body` as a mesh of QUADRILATERALS: every triangle of mesh_surface_tri cut into three, so the quadrilaterals are about half the size of the triangles.

            surface = mesh_surface_quad(part, 1.0)

        All the arguments are the ones of mesh_surface_tri. '''
    from fieldes.stdlib import sim_mesh
    mask = sim_mesh.symmetry_value(symmetry, 'mesh_surface_quad')
    with sim_mesh.surface_fit_scope(sim_mesh.current_surface_fit(), mask):
        return _surface('mesh_surface_quad', body, element_size, True, zones, default_zone, feature_angle, bounds, name)


def _layer_levels(layers, length, wide):
    ''' The distances along the axis of the faces of the layers of an extrusion, 0 ... length: a whole number of layers as high as the quadrilaterals are wide, or the thicknesses given (scaled to the length) '''
    if layers is None:
        n = max(1, int(round(length / wide)))
        return [length * k / n for k in range(n + 1)]
    if isinstance(layers, (int, float)):
        n = int(layers)
        if n < 1:
            raise ValueError('mesh_hex_extrusion: layers is a whole number above 0, or a list of the thicknesses of the layers')
        return [length * k / n for k in range(n + 1)]
    heights = [float(t) for t in layers]
    if not heights or min(heights) <= 0.0:
        raise ValueError('mesh_hex_extrusion: layers is a whole number above 0, or a list of the thicknesses of the layers (mm, above 0)')
    total = sum(heights)
    out = [0.0]
    for t in heights:
        out.append(out[-1] + t * length / total)
    out[-1] = length
    return out


def _mesh_extrusion(body, element_size, axis, layers, smooth, zones, default_zone, feature_angle, bounds, name):
    ''' Salome's Extrusion 3D: the face at one end of a prism meshed with quadrilaterals and swept through its length in layers.  Hexahedra only, and -- which a grid of cubes cannot do -- the surface fitted:
        the walls of a hole are the hole's.  For a body that has the same section all along an axis (a plate with holes, a bar, a tube); any other body is refused.

            mesh = mesh_hex_extrusion(plate_with_holes, 1.0)             # along z, the cells about 1 mm across

        element_size    the size of the cells (mm): the end face is first meshed with triangles twice that size (the tetrahedral mesher's, which fits the rim), the two triangles of a pair that make a good quadrilateral
                        are one, and every quadrilateral is cut into four (every triangle that has no partner into three), so the cells are 0.6 to 1 of it across; default: about forty elements along the longest side
        axis            'x', 'y' or 'z': the direction of the prism
        layers          the number of layers (default: as high as the quadrilaterals are wide), or a list of their thicknesses (scaled to the length)
        smooth          rounds of moving the nodes of the end face's quadrilaterals toward the middle of their neighbours (the nodes on the rim stay on it); 0 for none
        zones, default_zone, feature_angle  as mesh_tet4's '''
    t0 = time.time()
    if not isinstance(body, Shape):
        raise TypeError('mesh_hex_extrusion: the body is a Shape, not {!r}'.format(type(body).__name__))
    if not (isinstance(axis, str) and axis.strip().lower() in ('x', 'y', 'z')):
        raise ValueError("mesh_hex_extrusion: axis is 'x', 'y' or 'z'")
    a = 'xyz'.index(axis.strip().lower())
    h = element_size
    if h is None:
        from fieldes.stdlib.fields import _shape_bounds, tight_extent
        found = tight_extent(body) if bounds is None and not getattr(body, '_bounds', None) else None
        lo, hi = found if found is not None else _shape_bounds(body, bounds)
        h = default_element_size(body, lo, hi, 40.0, DEFAULT_ELEMENTS, thin=False)
    if not isinstance(h, (int, float)) or not h > 0:
        raise MeshError('mesh_hex_extrusion: element_size is a number above 0 (mm)')
    h = float(h)
    # (the triangles of the end face are twice the size asked for; where they are too coarse for its curves -- a quadrilateral comes out turned inside out -- finer ones are tried)
    last = None
    for factor in (2.0, 1.6, 1.3, 1.0):
        try:
            return _extrusion_attempt(body, h, factor, a, layers, smooth, zones, default_zone, feature_angle, bounds, name, t0)
        except _CoarseEnd as e:
            last = e
    raise MeshError(str(last))


class _CoarseEnd(MeshError):
    ''' The triangles of the end face are too coarse for its curves '''


def _mirror_quads(pos, quads, axis, at, tol):
    ''' (pos, quads) with the mirror image about the plane `axis` = `at` added (H226): the nodes within `tol` of the plane are put on it and shared, every other node gets an image, every quadrilateral its image turned
        the other way (so that the cells made from them keep the one sense) '''
    new_pos = [list(p) for p in pos]
    image = {}
    for n in sorted({n for q in quads for n in q}):
        p = pos[n]
        if abs(p[axis] - at) <= tol:
            new_pos[n][axis] = at
            image[n] = n
        else:
            q = list(p)
            q[axis] = 2.0 * at - p[axis]
            new_pos.append(q)
            image[n] = len(new_pos) - 1
    out = list(quads)
    for q in quads:
        out.append((image[q[0]], image[q[3]], image[q[2]], image[q[1]]))
    return new_pos, out


def _extrusion_attempt(body, h, factor, a, layers, smooth, zones, default_zone, feature_angle, bounds, name, t0):
    ''' One try of mesh_extrusion with triangles of `factor` times the element size on the end face '''
    # the end face: the boundary triangles of the tetrahedral mesh that look away from the axis and lie at its low end
    from fieldes.stdlib import sim_mesh
    tet = mesh_tet4(body, factor * h, bounds=bounds, symmetry=sim_mesh.symmetry_letters(sim_mesh.current_symmetry()))
    P = tet.nodes
    nn = len(P) // 3
    z0 = min(P[3 * i + a] for i in range(nn))
    z1 = max(P[3 * i + a] for i in range(nn))
    length = z1 - z0
    if not length > 0:
        raise MeshError('mesh_hex_extrusion: the body has no length along the {} axis'.format('xyz'[a]))
    tris = tet.faces.get('tri3')
    base = []
    if tris:
        for i in range(len(tris) // 3):
            f = tris[3 * i:3 * i + 3]
            p0, p1, p2 = [(P[3 * n], P[3 * n + 1], P[3 * n + 2]) for n in f]
            ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
            vx, vy, vz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
            nrm = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
            size = math.sqrt(nrm[0] ** 2 + nrm[1] ** 2 + nrm[2] ** 2)
            if size > 0.0 and nrm[a] / size < -0.95 and (p0[a] + p1[a] + p2[a]) / 3.0 < z0 + 0.3 * h:
                base.append((f[0], f[2], f[1]))                 # (turned to face along the axis: the boundary face looks away from it)
    if not base:
        raise MeshError('mesh_hex_extrusion: the body has no flat end face perpendicular to the {} axis: it is not a prism along it'.format('xyz'[a]))
    # (H226: a body that is symmetric about a plane along the axis has an end face that is symmetric about it, and the tetrahedral mesh it was taken from is the mirror image of a half.  The pairing of triangles and the
    # smoothing below go through the end face in an order that a mirror reverses, so the quadrilaterals are made on one side of the plane and mirrored: the plane is a wall of the half, as it is for the tetrahedra)
    planes = []
    if getattr(tet, 'mirrored', 0) and getattr(tet, 'box', None) is not None:
        planes = [(ax, at) for ax, at in _mirror_planes(tet.body, tet.box[0], tet.box[1], factor * h) if ax != a]
    tol = 1e-6 * h

    def on_plane(q, ax, at):
        return abs(q[ax] - at) <= tol

    if planes:
        def side(n, ax, at):
            c = P[3 * n + ax] - at
            return 0 if abs(c) <= tol else (1 if c > 0 else -1)
        if all(len({side(n, ax, at) for n in f} - {0}) <= 1 for f in base for ax, at in planes):
            base = [f for f in base if all(side(n, ax, at) >= 0 for n in f for ax, at in planes)]
        else:
            planes = []                                         # (a triangle with nodes on both sides of a plane: the end face is not cut cleanly by it, and is made whole)
    if not base:
        raise MeshError('mesh_hex_extrusion: the body has no flat end face perpendicular to the {} axis: it is not a prism along it'.format('xyz'[a]))
    # the nodes of the end face, then the middle of every edge and of every triangle (each triangle becomes three quadrilaterals)
    used = sorted(set(n for f in base for n in f))
    index = {n: i for i, n in enumerate(used)}
    pos = []
    for n in used:
        q = [P[3 * n], P[3 * n + 1], P[3 * n + 2]]
        q[a] = z0                                               # (a node of the rim a hair off the plane goes along the axis onto it: the side of a prism is not moved by that)
        pos.append(q)
    use = {}
    for f in base:
        for k in range(3):
            x_, y_ = index[f[k]], index[f[(k + 1) % 3]]
            key = (x_, y_) if x_ < y_ else (y_, x_)
            use[key] = use.get(key, 0) + 1
    mid = {}
    rim_middles = []

    def plane_edge(p, q):
        ''' An edge along a plane of the mirror (both nodes on it): the wall of the half, not the rim of the body '''
        return any(on_plane(pos[p], ax, at) and on_plane(pos[q], ax, at) for ax, at in planes)

    def middle(p, q):
        key = (p, q) if p < q else (q, p)
        m = mid.get(key)
        if m is None:
            m = len(pos)
            mid[key] = m
            pos.append([(pos[p][c] + pos[q][c]) * 0.5 for c in range(3)])
            if use.get(key) == 1 and not plane_edge(p, q):
                rim_middles.append(m)                           # (the middle of an edge of the rim lies on its chord: it goes onto the surface)
        return m

    u, v = (a + 1) % 3, (a + 2) % 3

    def corner_sine(ring):
        ''' The smallest sine of the angles of a polygon seen along the axis (1: right angles; a corner that is not convex has a negative one) '''
        low = 1e300
        m_ = len(ring)
        for k in range(m_):
            p0, p1, p2 = pos[ring[k]], pos[ring[(k + 1) % m_]], pos[ring[(k - 1) % m_]]
            ex, ey = p1[u] - p0[u], p1[v] - p0[v]
            fx, fy = p2[u] - p0[u], p2[v] - p0[v]
            size_ = math.hypot(ex, ey) * math.hypot(fx, fy)
            low = min(low, (ex * fy - ey * fx) / size_ if size_ > 0 else -1.0)
        return low

    # two triangles that share an edge are one quadrilateral where they make a good one (every corner between 30 and 150 degrees), the best first; a triangle that finds no partner stays one
    on_edge = {}
    for ti, f in enumerate(base):
        for k in range(3):
            x_, y_ = index[f[k]], index[f[(k + 1) % 3]]
            on_edge.setdefault((x_, y_) if x_ < y_ else (y_, x_), []).append((ti, k))
    pairs = []
    for key, items in on_edge.items():
        if len(items) != 2:
            continue
        (t1, k1), (t2, k2) = items
        f1, f2 = base[t1], base[t2]
        a_, b_ = index[f1[k1]], index[f1[(k1 + 1) % 3]]
        if index[f2[k2]] != b_ or index[f2[(k2 + 1) % 3]] != a_:
            continue                                            # (the two do not face the same way: not a sheet)
        ring = (a_, index[f2[(k2 + 2) % 3]], b_, index[f1[(k1 + 2) % 3]])
        sine = corner_sine(ring)
        if sine >= 0.5:
            pairs.append((sine, t1, t2, ring))
    pairs.sort(key=lambda t: -t[0])
    taken = set()
    cells = []                                                  # (the corners of a cell of the end face in order: a quadrilateral made of two triangles, or a triangle)
    for sine, t1, t2, ring in pairs:
        if t1 in taken or t2 in taken:
            continue
        taken.add(t1)
        taken.add(t2)
        cells.append(ring)
    for ti, f in enumerate(base):
        if ti not in taken:
            cells.append((index[f[0]], index[f[1]], index[f[2]]))
    # every quadrilateral is cut into four and every triangle into three, at the middles of their edges and of themselves: all quadrilaterals, and the neighbours share the middles of their edges
    quads = []
    centre_rings = []
    for ring in cells:
        m_ = len(ring)
        middles = [middle(ring[k], ring[(k + 1) % m_]) for k in range(m_)]
        c = len(pos)
        pos.append([sum(pos[n][k] for n in ring) / float(m_) for k in range(3)])
        centre_rings.append((c, tuple(ring) + tuple(middles)))
        for k in range(m_):
            quads.append((ring[k], middles[k], c, middles[(k - 1) % m_]))
    # the body must be a prism: over a grid on the box of its end face it is inside at the same places near the end, in the middle and near the other end
    # (the box of the whole end face: the nodes of the half, and the box mirrored about every plane, since the quadrilaterals of a symmetric body are made on one side of its plane and mirrored after this check)
    lo_u, hi_u = min(p[u] for p in pos), max(p[u] for p in pos)
    lo_v, hi_v = min(p[v] for p in pos), max(p[v] for p in pos)
    for ax, at in planes:
        if ax == u:
            lo_u = min(lo_u, 2.0 * at - hi_u)
        else:
            lo_v = min(lo_v, 2.0 * at - hi_v)
    grid = 60
    inside_at = []
    for height in (z0 + min(0.25 * h, 0.1 * length), z0 + 0.5 * length, z1 - min(0.25 * h, 0.1 * length)):
        pts = []
        for i in range(grid + 1):
            for j in range(grid + 1):
                q = [0.0, 0.0, 0.0]
                q[a] = height
                q[u] = lo_u + (hi_u - lo_u) * i / grid
                q[v] = lo_v + (hi_v - lo_v) * j / grid
                pts.append(tuple(q))
        inside_at.append(_eval(body, pts))
    # (a place within a thousandth of an element of the surface is not asked: the field there reads a hair either side of zero at different heights, and the box of a mirrored end face has its edges ON the tangent
    # points of a round wall -- 12 of 3,721 places of a tube along x were counted as different for that alone)
    near = 1e-3 * h
    differ = sum(1 for k in range(len(inside_at[0]))
                 if min(abs(inside_at[0][k]), abs(inside_at[1][k]), abs(inside_at[2][k])) > near and not ((inside_at[0][k] <= 0.0) == (inside_at[1][k] <= 0.0) == (inside_at[2][k] <= 0.0)))
    if differ > 0.003 * len(inside_at[0]):
        raise MeshError('mesh_hex_extrusion: the body is not a prism along the {} axis: {:,} of {:,} places over its end face are inside it at one height and outside it at another'.format('xyz'[a], differ, len(inside_at[0])))
    # the nodes of the rim go onto the surface -- the corners of the triangles too: the tetrahedral mesh's boundary vertices lie on the lattice, a fifth of a millimetre off a round hole at 2 mm -- along it, in the
    # plane (the field of a prism has no gradient along the axis); the middle of each triangle is then the mean of its six nodes
    rim_corners = set()
    for (p_, q_), count in use.items():
        if count == 1 and not plane_edge(p_, q_):
            rim_corners.update((p_, q_))
    on_rim = sorted(rim_corners) + rim_middles
    if on_rim:
        pts = []
        for m in on_rim:
            q = list(pos[m])
            q[a] = z0 + 0.5 * length
            pts.append(tuple(q))
        for m, q in zip(on_rim, _snap_to_surface(body, pts, [1.5 * h] * len(pts), 3)):
            was = list(pos[m])
            pos[m][u], pos[m][v] = q[u], q[v]
            for ax, at in planes:
                if on_plane(was, ax, at):
                    pos[m][ax] = at                             # (a node of the rim on the plane of the mirror stays on it: the Newton steps may leave it off by a hair)
        for c, ring in centre_rings:
            for k in range(3):
                pos[c][k] = sum(pos[n][k] for n in ring) / float(len(ring))
    # the nodes of the rim stay; the others are moved toward the middle of their neighbours when that makes the quadrilaterals round them better (a corner that is not turned inside out)
    edge_use = {}
    for q in quads:
        for k in range(4):
            e = (q[k], q[(k + 1) % 4]) if q[k] < q[(k + 1) % 4] else (q[(k + 1) % 4], q[k])
            edge_use[e] = edge_use.get(e, 0) + 1
    rim = set()
    neighbours = {}
    for (p, q_), count in edge_use.items():
        neighbours.setdefault(p, set()).add(q_)
        neighbours.setdefault(q_, set()).add(p)
        if count == 1 and not plane_edge(p, q_):
            rim.add(p)                                           # (an edge along a plane of the mirror is the wall of the half, not the rim: its nodes slide along the plane below)
            rim.add(q_)
    around = {}
    for qi, q in enumerate(quads):
        for n in q:
            around.setdefault(n, []).append(qi)

    def worst(qi):
        q = quads[qi]
        low = 1e300
        for k in range(4):
            p0, p1, p2 = pos[q[k]], pos[q[(k + 1) % 4]], pos[q[(k - 1) % 4]]
            ex, ey = p1[u] - p0[u], p1[v] - p0[v]
            fx, fy = p2[u] - p0[u], p2[v] - p0[v]
            size_ = math.hypot(ex, ey) * math.hypot(fx, fy)
            low = min(low, (ex * fy - ey * fx) / size_ if size_ > 0 else -1.0)
        return low

    moved = 0
    for _ in range(int(smooth) if smooth else 0):
        for n in range(len(pos)):
            if n in rim or n not in neighbours:
                continue
            before = min(worst(qi) for qi in around[n])
            old = (pos[n][u], pos[n][v])
            held = [(ax, at) for ax, at in planes if on_plane(pos[n], ax, at)]
            ns = neighbours[n]
            pos[n][u] = old[0] + 0.5 * (sum(pos[m][u] for m in ns) / len(ns) - old[0])
            pos[n][v] = old[1] + 0.5 * (sum(pos[m][v] for m in ns) / len(ns) - old[1])
            for ax, at in held:
                pos[n][ax] = at                                  # (a node on a plane of the mirror stays on it: it slides along it)
            after = min(worst(qi) for qi in around[n])
            if after >= before - 1e-12 and (after > 0.0 or after > before):
                moved += 1
            else:
                pos[n][u], pos[n][v] = old
    inverted = sum(1 for qi in range(len(quads)) if worst(qi) <= 0.0)
    if inverted:
        raise _CoarseEnd('mesh_hex_extrusion: {:,} of the {:,} quadrilaterals of the end face are turned inside out at this element size, and at the finer triangles that were tried (the curves of the face are finer than the element size): use a smaller element_size'.format(inverted, len(quads)))
    for ax, at in planes:
        pos, quads = _mirror_quads(pos, quads, ax, at, tol)
    # the layers: as high as the quadrilaterals are wide, in a whole number
    edge_sum = 0.0
    edge_count = 0
    for q in quads:
        for k in range(4):
            p0, p1 = pos[q[k]], pos[q[(k + 1) % 4]]
            edge_sum += math.hypot(p1[u] - p0[u], p1[v] - p0[v])
            edge_count += 1
    levels = _layer_levels(layers, length, edge_sum / edge_count if edge_count else h)
    count = len(pos)
    nodes = array('d')
    for k, dz in enumerate(levels):
        for p in pos:
            q = [p[0], p[1], p[2]]
            q[a] = z0 + dz if k < len(levels) - 1 else z1
            nodes.extend(q)
    H = array('i')
    for k in range(len(levels) - 1):
        lower, upper = k * count, (k + 1) * count
        for q in quads:
            H.extend((lower + q[0], lower + q[1], lower + q[2], lower + q[3], upper + q[0], upper + q[1], upper + q[2], upper + q[3]))
    mesh = Mesh(name or 'mesh')
    mesh.element = 'hex_extrusion'
    mesh.element_size = h
    mesh.body = body
    mesh.nodes = nodes
    mesh.cells['hex8'] = H
    _compact_nodes(mesh)
    mesh.faces = _boundary_of(mesh)
    _assign_zones(mesh, zones, default_zone, feature_angle, 0.02 * h)
    mesh.notes = ['the end face meshed with {:,} quadrilaterals (the triangles of its tetrahedral mesh, {:.2g} times the element size, paired into quadrilaterals where two make a good one, every quadrilateral cut '
                  'into four and every triangle left over into three, the nodes of the rim put on the surface, {:,} nodes moved toward the middle of their neighbours) and swept through {:.6g} mm in {} layers: '
                  '{:,} hexahedra, the surface fitted'.format(len(quads), factor, moved, length, len(levels) - 1, len(H) // 8)]
    if planes:
        mesh.notes.append('the body is symmetric about {} plane{} along the {} axis: the end face was made on one side of {} and mirrored, so the mesh is symmetric too'.format(
            len(planes), '' if len(planes) == 1 else 's', 'xyz'[a], 'it' if len(planes) == 1 else 'them'))
    mesh.seconds = time.time() - t0
    return mesh


def _tris_to_quads(mesh):
    ''' Every triangle cut into three quadrilaterals: the middle of its edges and of itself are new nodes '''
    P = mesh.nodes
    edge_node = {}
    tris = mesh.faces.get('tri3')
    zones = mesh.face_zone.get('tri3')
    if tris is None:
        return

    def mid(a, b):
        e = _edge(a, b)
        m = edge_node.get(e)
        if m is None:
            m = len(P) // 3
            edge_node[e] = m
            P.extend(((P[3 * a] + P[3 * b]) * 0.5, (P[3 * a + 1] + P[3 * b + 1]) * 0.5, (P[3 * a + 2] + P[3 * b + 2]) * 0.5))
        return m

    out = array('i')
    zone = array('i')
    for i in range(len(tris) // 3):
        a, b, c = tris[3 * i:3 * i + 3]
        ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
        centre = len(P) // 3
        P.extend(((P[3 * a] + P[3 * b] + P[3 * c]) / 3.0, (P[3 * a + 1] + P[3 * b + 1] + P[3 * c + 1]) / 3.0, (P[3 * a + 2] + P[3 * b + 2] + P[3 * c + 2]) / 3.0))
        out.extend((a, ab, centre, ca, b, bc, centre, ab, c, ca, centre, bc))
        zone.extend((zones[i],) * 3)
    mesh.faces = {'quad4': out}
    mesh.face_zone = {'quad4': zone}


################################################################################
# What FielDes draws

# A cell as tetrahedra, for the viewer's element display
_TET_SPLIT = {
    'hex8': ((0, 1, 2, 6), (0, 2, 3, 6), (0, 3, 7, 6), (0, 7, 4, 6), (0, 4, 5, 6), (0, 5, 1, 6)),
    'prism6': ((0, 1, 2, 5), (0, 1, 5, 4), (0, 4, 5, 3)),
    'pyra5': ((0, 1, 2, 4), (0, 2, 3, 4)),
}
_TET_SPLIT['hex20'] = _TET_SPLIT['hex8']         # (its corners are the first eight nodes)
MAX_DISPLAYED = 400000          # (tetrahedra the viewer is given: the cells of a larger mesh are not drawn)


def _hidden_local():
    ''' For each kind of cell that is drawn as tetrahedra: the pairs of its nodes (by their place in the cell) that are an edge of one of those tetrahedra and NOT an edge of the cell -- the diagonals
        of its quadrilaterals and the one through the cell.  The viewer draws the outline of a tetrahedron only on the edges that are not in this list, so that a hexahedron is drawn as the
        hexahedron it is, with its twelve edges (H203, the user, 2026-11-14: "THAT IS NOT A HEX ELEMENT. Those are Tets.") '''
    out = {}
    for kind, splits in _TET_SPLIT.items():
        real = set((min(a, b), max(a, b)) for a, b in EDGES[kind])
        hidden = set()
        for split in splits:
            for x in range(4):
                for y in range(x + 1, 4):
                    pair = (min(split[x], split[y]), max(split[x], split[y]))
                    if pair not in real:
                        hidden.add(pair)
        out[kind] = tuple(sorted(hidden))
    return out


def _display_tets(mesh):
    ''' Every cell as tetrahedra, right-handed: (the tetrahedra as a flat array, the cell of each, the first tetrahedron of every cell and one past the last, the nodes that the tetrahedra of the
        polyhedra have added -- the middle of each, a cone on every triangle of its faces -- as x y z after the mesh's own nodes, the pairs of nodes that are an edge of a tetrahedron and not of its cell:
        a set of (a, b) with a < b, which the viewer does not draw) '''
    P = mesh.nodes
    tets = array('i')
    owner = array('i')
    first = []
    extra = array('d')
    hidden = set()
    local = _hidden_local()
    gid = 0
    for kind in SOLIDS:
        conn = mesh.cells.get(kind)
        if not conn:
            continue
        n = NODES[kind]
        pairs = local.get(kind, ())
        for i in range(len(conn) // n):
            ids = conn[n * i:n * i + n]
            first.append(len(tets) // 4)
            parts = [tuple(ids[:4])] if kind in ('tet4', 'tet10') else [tuple(ids[j] for j in split) for split in _TET_SPLIT[kind]]
            for t in parts:
                if _det6(P, t[0], t[1], t[2], t[3]) < 0.0:
                    t = (t[1], t[0], t[2], t[3])
                tets.extend(t)
                owner.append(gid)
            for a, b in pairs:
                u, v = ids[a], ids[b]
                hidden.add((u, v) if u < v else (v, u))
            gid += 1
    for faces in mesh.polyhedra():
        first.append(len(tets) // 4)
        apex = len(P) // 3 + len(extra) // 3
        centre = _poly_centroid(P, faces)
        extra.extend(centre)
        for fc in faces:
            for j in range(len(fc)):
                hidden.add((fc[j], apex))                     # (the cone's edges to the middle: the apex has the largest number)
            for m in range(2, len(fc) - 1):
                u, v = fc[0], fc[m]                           # (the diagonals of the fan of the face: its polygon has only its sides)
                hidden.add((u, v) if u < v else (v, u))
        for fc in faces:
            for m in range(1, len(fc) - 1):
                a, b, c = fc[0], fc[m], fc[m + 1]
                ux, uy, uz = P[3 * b] - P[3 * a], P[3 * b + 1] - P[3 * a + 1], P[3 * b + 2] - P[3 * a + 2]
                vx, vy, vz = P[3 * c] - P[3 * a], P[3 * c + 1] - P[3 * a + 1], P[3 * c + 2] - P[3 * a + 2]
                wx, wy, wz = centre[0] - P[3 * a], centre[1] - P[3 * a + 1], centre[2] - P[3 * a + 2]
                if ux * (vy * wz - vz * wy) - uy * (vx * wz - vz * wx) + uz * (vx * wy - vy * wx) < 0.0:
                    tets.extend((a, c, b, apex))
                else:
                    tets.extend((a, b, c, apex))
                owner.append(gid)
        gid += 1
    first.append(len(tets) // 4)
    return tets, owner, first, extra, hidden


def _display_skin(mesh, body):
    ''' A surface mesh as what it is: its triangles and quadrilaterals, each coloured by its size, so that every element type of the Mesh menu is seen as itself.  The viewer is
        given cells, so every face stands as the fan of tetrahedra over it -- the face and an apex a little way inside, along its normal --, and the edges to the apex and the diagonals of a
        quadrilateral are not drawn.  Returns (the channels, the value of each channel for each tetrahedron, what the viewer is given for the cells, a note) '''
    from fieldes.ffi import lib as _lib
    from fieldes.shape import field_shape
    P = mesh.nodes
    nodes = len(P) // 3
    extra = array('d')
    tets = array('i')
    owner = array('i')
    faces = array('i')
    face_tet = array('i')
    hidden = set()
    sizes = []
    for kind in mesh.faces:
        for fn in _faces_of_kind(mesh, kind):
            n = len(fn)
            if n < 3:
                continue
            ax, ay, az = _polygon_area_vector(P, fn)
            area = math.sqrt(ax * ax + ay * ay + az * az)
            size = math.sqrt(4.0 * area / math.sqrt(3.0)) if n == 3 else math.sqrt(area)        # (the edge of the equilateral triangle / the side of the square of the same area)
            lift = 0.05 * (size or 1.0)
            length = area or 1.0
            cx, cy, cz = _centroid(P, fn)
            apex = nodes + len(extra) // 3
            extra.extend((cx - lift * ax / length, cy - lift * ay / length, cz - lift * az / length))
            gid = len(sizes)
            sizes.append(size)
            for j in range(n):
                hidden.add((fn[j], apex))                     # (the edges to the apex: the apex has the largest number)
            for m in range(1, n - 1):
                a, b, c = fn[0], fn[m], fn[m + 1]
                if m > 1:
                    hidden.add((a, c) if a < c else (c, a))   # (the diagonals of the fan of the face: its polygon has only its sides)
                ux, uy, uz = P[3 * b] - P[3 * a], P[3 * b + 1] - P[3 * a + 1], P[3 * b + 2] - P[3 * a + 2]
                vx, vy, vz = P[3 * c] - P[3 * a], P[3 * c + 1] - P[3 * a + 1], P[3 * c + 2] - P[3 * a + 2]
                wx, wy, wz = extra[-3] - P[3 * a], extra[-2] - P[3 * a + 1], extra[-1] - P[3 * a + 2]
                if ux * (vy * wz - vz * wy) - uy * (vx * wz - vz * wx) + uz * (vx * wy - vy * wx) < 0.0:
                    tets.extend((a, c, b, apex))
                else:
                    tets.extend((a, b, c, apex))
                owner.append(gid)
                faces.extend((a, b, c))
                face_tet.append(len(tets) // 4 - 1)
    ntets = len(tets) // 4
    if not ntets:
        return [], [], None, ''
    if ntets > MAX_DISPLAYED:
        return [], [], None, '{:,} faces: more than the {:,} that are drawn, so the elements are not shown'.format(len(sizes), MAX_DISPLAYED)
    lo, hi = min(sizes), max(sizes)
    if not hi > lo:
        half = 0.5 * abs(lo) if lo != 0.0 else 0.5               # (every face has the same size: the middle of the colour map, as for a grid of cubes)
        lo, hi = lo - half, lo + half
    constant = body * 0.0 + float(sum(sizes) / len(sizes))
    channels = [('size', 'Element size (mm) - per element', field_shape(_lib.libfive_tree_copy(constant.ptr)), lo, hi)]
    values = [array('f', [sizes[owner[t]] for t in range(ntets)]).tobytes()]
    verts = array('f', P)
    verts.extend(array('f', extra))
    hidden_pairs = array('i')
    for u, v in sorted(hidden):
        hidden_pairs.extend((u, v))
    fea_mesh = (verts.tobytes(), tets.tobytes(), faces.tobytes(), face_tet.tobytes(),
                '{:,} faces · {:,} nodes'.format(len(sizes), mesh.node_count), values, array('f', [0.0] * len(verts)).tobytes(),
                hidden_pairs.tobytes(), owner.tobytes())
    return channels, values, fea_mesh, ''


def _display_of(mesh, extra=None):
    ''' The body coloured by a field with the cells of the mesh behind it: what a displayed Mesh is to the viewer (see interpreter.cpp: _color_fields, _fea_mesh).  `extra`: fields of
        the result of a solver (see solvers.py) -- (name, label, the value of each tetrahedron as 32-bit floats, lowest, highest, mean) -- which come before the measures of the mesh '''
    from fieldes.ffi import lib as _lib
    from fieldes.shape import field_shape
    from fieldes.stdlib.fea import colored
    body = mesh.body
    if body is None:
        raise MeshError('this mesh has no body to draw it on')
    P = mesh.nodes
    channels = []
    values = []
    fea_mesh = None
    note = ''
    cells = mesh.cell_count()
    if cells:
        tets, owner, first, added, hidden = _display_tets(mesh)
        ntets = len(tets) // 4
        if ntets > MAX_DISPLAYED:
            note = '{:,} cells: more than the {:,} that are drawn, so the elements are not shown'.format(ntets, MAX_DISPLAYED)
        else:
            for name, label, data, lo, hi, mean in (extra or ()):
                constant = body * 0.0 + float(mean)
                channels.append((name, label, field_shape(_lib.libfive_tree_copy(constant.ptr)), lo, hi))
                values.append(data)
            q = _measure(mesh, False, 150000, collect=True)
            labels = (('size', 'Element size (mm)'), ('scaled_jacobian', 'Scaled Jacobian (1 best)'), ('skewness', 'Skewness (0 regular, 1 degenerate)'), ('aspect_ratio', 'Aspect ratio'),
                      ('orthogonal_quality', 'Orthogonal quality (1 best)'), ('volume_ratio', 'Volume ratio to the neighbours'))
            for name, label in labels:
                per = q.per_cell.get(name)
                if not per:
                    continue
                lo, hi = min(per.values()), max(per.values())
                if name == 'aspect_ratio' or name == 'volume_ratio':
                    hi = min(hi, 100.0)
                if not hi > lo:
                    # (every cell has the same value -- a grid of cubes of one size: the colour of the middle of the map, not the dark end that the lowest value of a range of no width gets: the mesh
                    # drawn all black, and its edges lost on it)
                    half = 0.5 * abs(lo) if lo != 0.0 else 0.5
                    lo, hi = lo - half, lo + half
                mean = sum(per.values()) / len(per)
                constant = body * 0.0 + float(mean)
                channels.append((name, label + ' - per element', field_shape(_lib.libfive_tree_copy(constant.ptr)), lo, hi))
                values.append(array('f', [per.get(owner[t], 0.0) for t in range(ntets)]).tobytes())
            # the boundary: the faces of the mesh as triangles, each with the tetrahedron it belongs to
            cell_of = {}
            gid = 0
            for kind in SOLIDS:
                conn = mesh.cells.get(kind)
                if not conn:
                    continue
                n, k = NODES[kind], CORNERS[kind]
                for i in range(len(conn) // n):
                    ids = conn[n * i:n * i + k]
                    for f in FACES[kind]:
                        cell_of[tuple(sorted(ids[j] for j in f))] = gid
                    gid += 1
            for fcs in mesh.polyhedra():
                for fc in fcs:
                    cell_of[tuple(sorted(fc))] = gid
                gid += 1
            faces = array('i')
            face_tet = array('i')
            for kind in mesh.faces:
                for fn in _faces_of_kind(mesh, kind):
                    n = len(fn)
                    c = cell_of.get(tuple(sorted(fn)))
                    if c is None:
                        continue
                    for m in range(2, n - 1):
                        u, v = fn[0], fn[m]                   # (the diagonals of the fan this face is drawn with are not edges of the element)
                        hidden.add((u, v) if u < v else (v, u))
                    for tri in ((fn,) if n == 3 else tuple((fn[0], fn[m], fn[m + 1]) for m in range(1, n - 1))):
                        best, most = first[c], -1
                        for tt in range(first[c], first[c + 1]):
                            have = sum(1 for v in tri if v in tets[4 * tt:4 * tt + 4])
                            if have > most:
                                best, most = tt, have
                        faces.extend(tri)
                        face_tet.append(best)
            verts = array('f', P)
            verts.extend(array('f', added))
            # (items 8 and 9, for the viewer to draw the cells as the cells they are, H203: the pairs of nodes of the tetrahedra's edges that are not an edge of a cell, which it does not draw, and the cell of
            # each tetrahedron, which the section view keeps or cuts away whole)
            hidden_pairs = array('i')
            for u, v in sorted(hidden):
                hidden_pairs.extend((u, v))
            fea_mesh = (verts.tobytes(), tets.tobytes(), faces.tobytes(), face_tet.tobytes(),
                        '{:,} cells · {:,} nodes'.format(cells, mesh.node_count), values, array('f', [0.0] * len(verts)).tobytes(),
                        hidden_pairs.tobytes(), owner.tobytes())
    elif mesh.face_count():
        channels, values, fea_mesh, note = _display_skin(mesh, body)
    if not channels:
        # (a mesh that is not drawn -- too many cells -- or has no faces: the body, with a size that is the one the mesh has)
        size = float(mesh.element_size or 1.0)
        constant = body * 0.0 + size
        channels.append(('size', 'Element size (mm)', field_shape(_lib.libfive_tree_copy(constant.ptr)), size * 0.5, size * 1.5))
    name, label, field, lo, hi = channels[0]
    out = colored(body, field, range=(lo, hi), label=label, colormap='turbo')
    out._color_fields = channels
    out._color_field_name = name
    out._fea_mesh = fea_mesh
    out._elements_first = True
    out._cells_only = True        # (a mesh is its cells: the body that the viewer would mesh from its field is not the mesh -- it cuts corners that the mesh has not -- and is not drawn)
    if fea_mesh is None and note:
        out._fea_elements_note = note
    return out


################################################################################
# Layers of prisms

def _add_layers(mesh, tets, wall_distance, layers, notes):
    ''' The prisms: every face of the core that lies on the surface at the thickness of the layers is the base of a column of prisms up to the wall '''
    heights = layers.heights()
    thickness = sum(heights)
    n = len(heights)
    P = mesh.nodes
    # the boundary faces of the core, and which of them are the surface the layers grow from: those whose three corners lie at `thickness` from the wall and that face
    # away from it (a face of the floor that is close to the wall has a corner at that distance too, but it is not turned to the wall)
    _, faces = _boundary_edges(tets)
    corner = {}
    for f in faces:
        for v in f:
            corner[v] = None
    ids = sorted(corner)
    values = _eval(wall_distance, [(P[3 * v], P[3 * v + 1], P[3 * v + 2]) for v in ids])
    corner = dict(zip(ids, values))
    near = []
    for f in faces:
        edge = math.sqrt(sum((P[3 * f[0] + a] - P[3 * f[1] + a]) ** 2 for a in range(3)))
        if all(abs(corner[v] - thickness) < max(0.1 * thickness, 0.15 * edge) for v in f):
            near.append(f)
    interface = []
    if near:
        step = 0.05 * thickness
        samples = []
        for f in near:
            c = _centroid(P, f)
            samples.extend(((c[0] + step, c[1], c[2]), (c[0] - step, c[1], c[2]), (c[0], c[1] + step, c[2]), (c[0], c[1] - step, c[2]), (c[0], c[1], c[2] + step), (c[0], c[1], c[2] - step)))
        g = _eval(wall_distance, samples)
        for i, f in enumerate(near):
            ax, ay, az = _polygon_area_vector(P, f)
            gx, gy, gz = g[6 * i] - g[6 * i + 1], g[6 * i + 2] - g[6 * i + 3], g[6 * i + 4] - g[6 * i + 5]
            norm = math.sqrt(ax * ax + ay * ay + az * az) * math.sqrt(gx * gx + gy * gy + gz * gz)
            if norm > 0.0 and abs(ax * gx + ay * gy + az * gz) > 0.7 * norm:
                interface.append(f)
    if not interface:
        notes.append('no layers: the core has no surface at {:.4g} mm from the wall'.format(thickness))
        return
    nodes = sorted(set(v for f in interface for v in f))
    points = [(P[3 * v], P[3 * v + 1], P[3 * v + 2]) for v in nodes]
    # the way to the wall: against the gradient of the distance, `thickness` of it, then onto the wall itself
    wall = _walk_to_wall(wall_distance, points, thickness)
    level = {}
    # (the cumulative thickness from the wall: level j has the nodes at thickness - the heights from the core)
    cum = [0.0]
    for h in heights:
        cum.append(cum[-1] + h)
    for v, p0, q in zip(nodes, points, wall):
        row = [v]
        for j in range(1, n + 1):
            f = cum[n - j] / cum[n]          # (from the wall: 1 at the core, 0 at the wall)
            if j == n:
                f = 0.0
            m = len(P) // 3
            P.extend((q[0] + (p0[0] - q[0]) * f, q[1] + (p0[1] - q[1]) * f, q[2] + (p0[2] - q[2]) * f))
            row.append(m)
        level[v] = row
    # (the faces of the core on the interface are shared by its tetrahedra and the first prisms: they are not the boundary any more)
    prisms = array('i')
    for f in interface:
        a, b, c = f
        for j in range(n):
            prisms.extend((level[a][j], level[b][j], level[c][j], level[a][j + 1], level[b][j + 1], level[c][j + 1]))
    mesh.cells['prism6'] = prisms
    bad = 0
    for i in range(len(prisms) // 6):
        if _cell_volume(P, 'prism6', prisms[6 * i:6 * i + 6]) <= 0.0:
            bad += 1
    if bad:
        notes.append('!! {:,} of the {:,} prisms are inside out or flat: the layers are too thick for the surface (round the corners, or fewer layers)'.format(bad, len(prisms) // 6))
    notes.append('{:,} prisms in {} layers on {:,} faces'.format(len(prisms) // 6, n, len(interface)))


def _add_hex_layers(mesh, wall_distance, layers, notes):
    ''' The layers of a mesh of hexahedra (Salome's Viscous Layers on Extrusion 3D): the step of _add_layers with quadrilaterals -- every quadrilateral of the boundary of the core that lies on
        the surface at the thickness of the layers and looks at the wall is the base of a column of hexahedra up to the wall, `layers` of them, the one at the wall `first_height` thick.  The quadrilaterals of the core are the
        faces of the first hexahedra, so core and layers meet in whole faces.  Where the hexahedra come out turned inside out (the layers too thick for the surface) the notes say how many '''
    heights = layers.heights()
    thickness = sum(heights)
    n = len(heights)
    P = mesh.nodes
    quads = _faces_of_kind(mesh, 'quad4') if 'quad4' in mesh.faces else []
    if not quads:
        notes.append('no layers: the core has no quadrilaterals on its boundary')
        return 0
    corner = {}
    for f in quads:
        for v in f:
            corner[v] = None
    ids = sorted(corner)
    values = _eval(wall_distance, [(P[3 * v], P[3 * v + 1], P[3 * v + 2]) for v in ids])
    corner = dict(zip(ids, values))
    near = []
    for f in quads:
        edge = math.sqrt(sum((P[3 * f[0] + a] - P[3 * f[1] + a]) ** 2 for a in range(3)))
        if all(abs(corner[v] - thickness) < max(0.1 * thickness, 0.15 * edge) for v in f):
            near.append(f)
    interface = []
    if near:
        step = 0.05 * thickness
        samples = []
        for f in near:
            c = _centroid(P, f)
            samples.extend(((c[0] + step, c[1], c[2]), (c[0] - step, c[1], c[2]), (c[0], c[1] + step, c[2]), (c[0], c[1] - step, c[2]), (c[0], c[1], c[2] + step), (c[0], c[1], c[2] - step)))
        g = _eval(wall_distance, samples)
        for i, f in enumerate(near):
            ax, ay, az = _polygon_area_vector(P, f)
            gx, gy, gz = g[6 * i] - g[6 * i + 1], g[6 * i + 2] - g[6 * i + 3], g[6 * i + 4] - g[6 * i + 5]
            norm = math.sqrt(ax * ax + ay * ay + az * az) * math.sqrt(gx * gx + gy * gy + gz * gz)
            if norm > 0.0 and abs(ax * gx + ay * gy + az * gz) > 0.7 * norm:
                interface.append(f)
    if not interface:
        notes.append('no layers: the core has no surface at {:.4g} mm from the wall'.format(thickness))
        return 0
    nodes = sorted(set(v for f in interface for v in f))
    points = [(P[3 * v], P[3 * v + 1], P[3 * v + 2]) for v in nodes]
    wall = _walk_to_wall(wall_distance, points, thickness)
    cum = [0.0]
    for h in heights:
        cum.append(cum[-1] + h)
    level = {}
    for v, p0, q in zip(nodes, points, wall):
        row = [v]
        for j in range(1, n + 1):
            f = 0.0 if j == n else cum[n - j] / cum[n]          # (from the wall: 1 at the core, 0 at the wall)
            m = len(P) // 3
            P.extend((q[0] + (p0[0] - q[0]) * f, q[1] + (p0[1] - q[1]) * f, q[2] + (p0[2] - q[2]) * f))
            row.append(m)
        level[v] = row
    hexes = array('i')
    bad = 0
    for f in interface:
        a, b, c, d = f
        # (the quadrilateral turned outward from the core is the bottom of the first hexahedron when its corners go a, b, c, d; if that comes out turned inside out, the other way round)
        order = (a, b, c, d)
        first = tuple(level[v][0] for v in order) + tuple(level[v][1] for v in order)
        if _cell_volume(P, 'hex8', first) < 0.0:
            order = (a, d, c, b)
        for j in range(n):
            ids8 = tuple(level[v][j] for v in order) + tuple(level[v][j + 1] for v in order)
            hexes.extend(ids8)
            if _cell_volume(P, 'hex8', ids8) <= 0.0:
                bad += 1
    old = mesh.cells.get('hex8')
    mesh.cells['hex8'] = array('i', list(old) + list(hexes)) if old else hexes
    if bad:
        notes.append('!! {:,} of the {:,} hexahedra of the layers are inside out or flat: the layers are too thick for the surface (round the corners, or fewer layers)'.format(bad, len(hexes) // 8))
    notes.append('{:,} hexahedra in {} layers on {:,} quadrilaterals'.format(len(hexes) // 8, n, len(interface)))
    return len(hexes) // 8


def _walk_to_wall(distance, points, thickness):
    ''' Where the wall is, from points at `thickness` from it: a step against the gradient of the distance, then Newton steps onto its surface '''
    cur = [list(p) for p in points]
    for it in range(4):
        samples = []
        e = 0.02 * thickness
        for p in cur:
            samples.extend(((p[0], p[1], p[2]), (p[0] + e, p[1], p[2]), (p[0] - e, p[1], p[2]), (p[0], p[1] + e, p[2]), (p[0], p[1] - e, p[2]),
                            (p[0], p[1], p[2] + e), (p[0], p[1], p[2] - e)))
        values = _eval(distance, samples)
        for i, p in enumerate(cur):
            v = values[7 * i:7 * i + 7]
            gx, gy, gz = (v[1] - v[2]) / (2 * e), (v[3] - v[4]) / (2 * e), (v[5] - v[6]) / (2 * e)
            g = math.sqrt(gx * gx + gy * gy + gz * gz)
            if g < 1e-9:
                continue
            k = v[0] / g
            p[0] -= k * gx / g
            p[1] -= k * gy / g
            p[2] -= k * gz / g
    return cur


################################################################################
# Quality

def _cell_metrics(P, kind, ids, v):
    ''' (skewness, aspect ratio, mean edge, smallest dihedral angle or None) of a cell of volume v '''
    if kind in ('tet4', 'tet10'):
        skew = _tet_skew(P, ids, v)
        dihedral = _tet_min_dihedral(P, ids)
    else:
        skew = _face_skew(P, kind, ids)
        dihedral = None
    lengths = []
    for a, b in EDGES[kind]:
        lengths.append(math.sqrt((P[3 * ids[a]] - P[3 * ids[b]]) ** 2 + (P[3 * ids[a] + 1] - P[3 * ids[b] + 1]) ** 2 + (P[3 * ids[a] + 2] - P[3 * ids[b] + 2]) ** 2))
    shortest = min(lengths)
    return skew, (max(lengths) / shortest if shortest > 0 else 1e9), sum(lengths) / len(lengths), dihedral


_LEAVING = {
    'hex8': {0: (1, 3, 4), 1: (2, 0, 5), 2: (3, 1, 6), 3: (0, 2, 7), 4: (7, 5, 0), 5: (4, 6, 1), 6: (5, 7, 2), 7: (6, 4, 3)},
    'prism6': {0: (1, 2, 3), 1: (2, 0, 4), 2: (0, 1, 5), 3: (5, 4, 0), 4: (3, 5, 1), 5: (4, 3, 2)},
    'pyra5': {0: (1, 3, 4), 1: (2, 0, 4), 2: (3, 1, 4), 3: (0, 2, 4)},          # (the four corners of the base: the apex has four edges and no one way to take three of them)
}
_LEAVING['hex20'] = _LEAVING['hex8']


def _cell_jacobian(P, kind, ids):
    ''' The scaled Jacobian of a cell, the number mesh suites report (Salome's, Gmsh's "SICN", Cubit's "scaled jacobian"): at each corner, the determinant of the three edges that leave it
        over the product of their lengths, and the smallest of them.  1 for a regular tetrahedron, a cube and any box, 0 where a corner is flat, below 0 for a cell turned inside out '''
    if kind in ('tet4', 'tet10'):
        d = _det6(P, ids[0], ids[1], ids[2], ids[3])
        worst = 1e30
        for c in range(4):
            prod = 1.0
            for o in range(4):
                if o != c:
                    prod *= math.sqrt((P[3 * ids[o]] - P[3 * ids[c]]) ** 2 + (P[3 * ids[o] + 1] - P[3 * ids[c] + 1]) ** 2 + (P[3 * ids[o] + 2] - P[3 * ids[c] + 2]) ** 2)
            worst = min(worst, math.sqrt(2.0) * d / prod if prod > 0 else -1.0)
        return worst
    worst = 1e30
    for c, (a, b, e) in _LEAVING[kind].items():
        o = 3 * ids[c]
        ox, oy, oz = P[o], P[o + 1], P[o + 2]
        q = 3 * ids[a]
        ux, uy, uz = P[q] - ox, P[q + 1] - oy, P[q + 2] - oz
        q = 3 * ids[b]
        vx, vy, vz = P[q] - ox, P[q + 1] - oy, P[q + 2] - oz
        q = 3 * ids[e]
        wx, wy, wz = P[q] - ox, P[q + 1] - oy, P[q + 2] - oz
        det = ux * (vy * wz - vz * wy) - uy * (vx * wz - vz * wx) + uz * (vx * wy - vy * wx)
        n = math.sqrt((ux * ux + uy * uy + uz * uz) * (vx * vx + vy * vy + vz * vz) * (wx * wx + wy * wy + wz * wz))
        worst = min(worst, det / n if n > 0 else -1.0)
    return worst


def _measure(mesh, full, max_cells, collect=False):
    ''' The quality of the mesh (see Mesh.quality).  `collect` keeps the value of every cell, in the order of cell_ids(), as q.per_cell[name] -- what the viewer
        colours the elements by '''
    t0 = time.time()
    q = MeshQuality()
    P = mesh.nodes
    total = mesh.cell_count()
    q.counts = {k: mesh.cell_count(k) for k in SOLIDS + (POLY,)}
    stride = 1
    neighbours = full or total <= max_cells
    per = {'size': {}, 'skewness': {}, 'aspect_ratio': {}, 'scaled_jacobian': {}}
    jacobian = []
    if not neighbours and not collect:
        stride = max(2, total // max_cells)
        q.sampled = stride
        q.notes.append('The mesh has {:,} cells: orthogonal quality and volume ratio need the faces of all of them and are not measured (quality(full=True) measures them)'.format(total))
    skew, aspect, dihedral = [], [], []
    sizes = []                  # (the volume of each cell that is measured: how much the sizes vary, see MeshQuality.spread)
    centroid = {}
    volume = {}
    faces = {}                  # key -> [cell, outward area vector, centre of the face, (second cell)]
    gid = 0
    cell_orth = {}
    def every_cell():
        for kind in SOLIDS:
            conn = mesh.cells.get(kind)
            if not conn:
                continue
            n = NODES[kind]
            k = CORNERS[kind]
            for i in range(len(conn) // n):
                yield kind, i, conn[n * i:n * i + k], None
        for i, fcs in enumerate(mesh.polyhedra()):
            yield POLY, i, None, fcs

    for kind, i, ids, fcs in every_cell():
        my = gid
        gid += 1
        if kind == POLY:
            v = _poly_volume(P, fcs)
        else:
            v = _cell_volume(P, kind, ids)
        q.volume += v
        if v <= 0.0:
            q.negative += 1
        if not neighbours and (i % stride):
            continue
        if kind == POLY:
            cx, cy, cz = _poly_centroid(P, fcs)
            s, a_, mean_edge = _poly_metrics(P, fcs)
            dih = None
            jac = None
        else:
            cx, cy, cz = _centroid(P, ids)
            s, a_, mean_edge, dih = _cell_metrics(P, kind, ids, v)
            jac = _cell_jacobian(P, kind, ids)
        centroid[my] = (cx, cy, cz)
        volume[my] = v
        skew.append(s)
        sizes.append(v)
        aspect.append(a_)
        if jac is not None:
            jacobian.append(jac)
        if dih is not None:
            dihedral.append(dih)
        if collect:
            per['size'][my] = mean_edge
            per['skewness'][my] = s
            per['aspect_ratio'][my] = a_
            if jac is not None:
                per['scaled_jacobian'][my] = jac
        if neighbours:
            for fid in (fcs if kind == POLY else [[ids[j] for j in face] for face in FACES[kind]]):
                key = tuple(sorted(fid))
                area = _polygon_area_vector(P, fid)
                centre = _centroid(P, fid)
                entry = faces.get(key)
                if entry is None:
                    faces[key] = [my, area, centre, None]
                else:
                    entry[3] = my
                # (the cosine between the area vector and the way from the middle of the cell to the face)
                cell_orth[my] = min(cell_orth.get(my, 1.0), _cosine(area, (centre[0] - cx, centre[1] - cy, centre[2] - cz)))
    q.metrics['skewness'] = _stats(skew)
    q.skewed = (sum(1 for s in skew if s > 0.85), sum(1 for s in skew if s > 0.95), len(skew))
    positive = sorted(s for s in sizes if s > 0.0)
    if len(positive) >= 20:
        median = positive[len(positive) // 2]
        q.spread = (positive[-1] / positive[0], positive[len(positive) // 20] / median, positive[-1 - len(positive) // 20] / median)
        q.graded = bool(getattr(mesh, 'graded', False))
    q.metrics['aspect_ratio'] = _stats(aspect)
    if jacobian:
        q.metrics['scaled_jacobian'] = _stats(jacobian)
        q.poor_jacobian = sum(1 for j in jacobian if j < 0.2)
    if dihedral:
        q.metrics['min_dihedral'] = _stats(dihedral)
    if neighbours:
        ratios = {}
        for entry in faces.values():
            c0, area, centre, c1 = entry
            if c1 is None:
                continue
            d = (centroid[c1][0] - centroid[c0][0], centroid[c1][1] - centroid[c0][1], centroid[c1][2] - centroid[c0][2])
            along = _cosine(area, d)
            cell_orth[c0] = min(cell_orth[c0], along)
            # (seen from the other cell the area vector points the other way, and so does the way to the neighbour)
            cell_orth[c1] = min(cell_orth[c1], along)
            r = volume[c0] / volume[c1] if volume[c1] > 0 else 1e9
            for c, rr in ((c0, r), (c1, 1.0 / r if r else 1e9)):
                ratios[c] = max(ratios.get(c, 1.0), rr)
        if cell_orth:
            q.metrics['orthogonal_quality'] = _stats(list(cell_orth.values()))
            per['orthogonal_quality'] = cell_orth
        if ratios:
            q.metrics['volume_ratio'] = _stats(list(ratios.values()))
            per['volume_ratio'] = ratios
    q.per_cell = per if collect else None
    _volume_note(mesh, q)
    q.seconds = time.time() - t0
    return q


def _ramp_volume(body, box, step):
    ''' The volume of the body in the box, from the field on a grid of cells of about `step`: a sample counts as much of its cell as its distance to the surface says -- all of it at half a cell inside, none at
        half a cell outside, a linear ramp between (the body's field is a distance), which is exact for a flat wall that is as thick as the cell or thicker whatever its place among the samples '''
    from fieldes.stdlib.fields import sample_grid
    size = [float(box[1][i]) - float(box[0][i]) for i in range(3)]
    n = [max(1, int(math.ceil(s / step))) for s in size]
    _, values = sample_grid(body, box[0], box[1], n)
    edge = (size[0] * size[1] * size[2] / float(n[0] * n[1] * n[2])) ** (1.0 / 3.0)
    total = 0.0
    for v in values:
        if v < 0.5 * edge:
            c = 0.5 - v / edge
            total += 1.0 if c >= 1.0 else c
    return total * edge ** 3


def _volume_note(mesh, q):
    ''' A note when the mesh holds noticeably more or less than the body it was made of (5 % or more): a wall thinner than the elements falls between the nodes of the lattice of the tetrahedra
        and is lost without a word (the imported mobile stand of the examples at an element size of 2.8 mm kept 19 % of its volume), and the cubes of a grid add whole cubes along it.  The volume of
        the body is the share of 64 x 64 x 64 samples of the box it was meshed in that lies inside it (fields.mass_properties), and where that is 5 % or more off the mesh's, read again on a grid of half the
        element size (at most 1.5 million samples) with the distance as a ramp (_ramp_volume): a wall thinner than the first grid is not counted right by it.  Not made for a mesh that has no body or no box (read from a file, a
        sweep or a shell) '''
    body = getattr(mesh, 'body', None)
    box = getattr(mesh, 'box', None)
    if body is None or box is None or q.volume <= 0.0:
        return
    longest = max(float(box[1][i]) - float(box[0][i]) for i in range(3))
    if longest <= 0.0:
        return
    try:
        from fieldes.stdlib.fields import mass_properties
        reference = mass_properties(body, lower=box[0], upper=box[1], resolution=64.0 / longest)['volume']
    except Exception:
        return
    if not reference or reference <= 0.0:
        return
    ratio = q.volume / reference
    if abs(ratio - 1.0) >= 0.05:
        # (64 samples across the box say nothing about a wall thinner than their spacing -- the shell box of 1.2 mm walls, whose mesh held all of its 21,900 mm^3, was said to hold 78 % of 27,900: the volume
        # is read again, finer, from the distance itself: a sample counts as much of its cell as the distance to the surface says, which is exact for a wall as thick as the step or more)
        step = longest / 64.0
        element = float(getattr(mesh, 'element_size', 0) or 0)
        if element > 0.0:
            step = min(step, 0.5 * element)
        size = [float(box[1][i]) - float(box[0][i]) for i in range(3)]
        while size[0] * size[1] * size[2] / step ** 3 > 1.5e6:
            step *= 1.1
        try:
            reference = _ramp_volume(body, box, step)
        except Exception:
            return
        if not reference or reference <= 0.0:
            return
        ratio = q.volume / reference
    if abs(ratio - 1.0) >= 0.05:
        q.notes.append('The mesh holds {:.0f} % of the volume of the body ({:,.4g} of about {:,.4g} mm^3): {}'.format(
            100.0 * ratio, q.volume, reference,
            'walls and parts thinner than the elements fall between them and are lost -- an element size of half the thinnest wall or less keeps them' if ratio < 1.0
            else 'the cells that the surface cuts are whole cubes, which add volume along the surface -- a smaller element size, or a size that follows the surface, takes it back'))


def _stats(values):
    if not values:
        return (0.0, 0.0, 0.0)
    return (min(values), sum(values) / len(values), max(values))


def _poly_metrics(P, faces):
    """ (skewness, aspect ratio, mean edge) of a polyhedron: the skewness by angle of its worst face -- each angle of a polygon of n corners against the (n - 2) 180 / n degrees of the regular one --,
        the longest edge over the shortest, the mean of the edges """
    edges = set()
    worst = 0.0
    for fc in faces:
        n = len(fc)
        for k in range(n):
            a, b = fc[k], fc[(k + 1) % n]
            edges.add((a, b) if a < b else (b, a))
        corners = list(fc)
        angles = _angles(P, corners)
        # (a node that lies on the straight line between its two neighbours -- the cut of a cell follows a straight crease of the body through the nodes on it -- is no corner of the face: its angle of 180 degrees
        # made a cell of a sound shape "degenerate", skewness 1, in the report of a mesh whose nodes are on a crease)
        while len(corners) > 3:
            k = min(range(len(corners)), key=lambda m: abs(angles[m] - 180.0))
            if abs(angles[k] - 180.0) > 1.0:
                break
            del corners[k]
            angles = _angles(P, corners)
        n = len(corners)
        ideal = (n - 2) * 180.0 / n
        s = max((max(angles) - ideal) / (180.0 - ideal), (ideal - min(angles)) / ideal)
        worst = max(worst, s)
    lengths = [math.sqrt((P[3 * a] - P[3 * b]) ** 2 + (P[3 * a + 1] - P[3 * b + 1]) ** 2 + (P[3 * a + 2] - P[3 * b + 2]) ** 2) for a, b in edges]
    shortest = min(lengths)
    return max(0.0, min(1.0, worst)), (max(lengths) / shortest if shortest > 0 else 1e9), sum(lengths) / len(lengths)


def _cosine(a, b):
    la = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
    lb = math.sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2])
    if la == 0 or lb == 0:
        return 0.0
    return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb)


def _tet_skew(P, ids, v):
    ''' Skewness by volume: the volume of the regular tetrahedron that has the circumscribed sphere of this one, less its volume, over it '''
    a, b, c, d = ids
    ax, ay, az = P[3 * b] - P[3 * a], P[3 * b + 1] - P[3 * a + 1], P[3 * b + 2] - P[3 * a + 2]
    bx, by, bz = P[3 * c] - P[3 * a], P[3 * c + 1] - P[3 * a + 1], P[3 * c + 2] - P[3 * a + 2]
    cx, cy, cz = P[3 * d] - P[3 * a], P[3 * d + 1] - P[3 * a + 1], P[3 * d + 2] - P[3 * a + 2]
    det = ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx)
    if abs(det) < 1e-300:
        return 1.0
    a2, b2, c2 = ax * ax + ay * ay + az * az, bx * bx + by * by + bz * bz, cx * cx + cy * cy + cz * cz
    # circumcentre relative to a: (a2 (b x c) + b2 (c x a) + c2 (a x b)) / (2 det)
    ux = a2 * (by * cz - bz * cy) + b2 * (cy * az - cz * ay) + c2 * (ay * bz - az * by)
    uy = a2 * (bz * cx - bx * cz) + b2 * (cz * ax - cx * az) + c2 * (az * bx - ax * bz)
    uz = a2 * (bx * cy - by * cx) + b2 * (cx * ay - cy * ax) + c2 * (ax * by - ay * bx)
    radius = math.sqrt(ux * ux + uy * uy + uz * uz) / (2.0 * abs(det))
    regular = 8.0 * radius ** 3 / (9.0 * math.sqrt(3.0))
    return max(0.0, min(1.0, (regular - v) / regular)) if regular > 0 else 1.0


def _angles(P, ids):
    ''' The angles of a polygon in degrees '''
    out = []
    n = len(ids)
    for i in range(n):
        p, q, r = ids[i - 1], ids[i], ids[(i + 1) % n]
        ux, uy, uz = P[3 * p] - P[3 * q], P[3 * p + 1] - P[3 * q + 1], P[3 * p + 2] - P[3 * q + 2]
        vx, vy, vz = P[3 * r] - P[3 * q], P[3 * r + 1] - P[3 * q + 1], P[3 * r + 2] - P[3 * q + 2]
        lu, lv = math.sqrt(ux * ux + uy * uy + uz * uz), math.sqrt(vx * vx + vy * vy + vz * vz)
        if lu == 0 or lv == 0:
            out.append(0.0)
            continue
        c = max(-1.0, min(1.0, (ux * vx + uy * vy + uz * vz) / (lu * lv)))
        out.append(math.degrees(math.acos(c)))
    return out


def _face_skew(P, kind, ids):
    ''' Skewness by angle: the worst face's angles against the ideal ones (60 degrees of a triangle, 90 of a quadrilateral) '''
    worst = 0.0
    for face in FACES[kind]:
        angles = _angles(P, [ids[i] for i in face])
        ideal = 60.0 if len(face) == 3 else 90.0
        s = max((max(angles) - ideal) / (180.0 - ideal), (ideal - min(angles)) / ideal)
        worst = max(worst, s)
    return max(0.0, min(1.0, worst))


def _tet_min_dihedral(P, ids):
    ''' The smallest dihedral angle of a tetrahedron, degrees '''
    best = 180.0
    for a, b in ((0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3)):
        c, d = [x for x in range(4) if x not in (a, b)]
        ia, ib, ic, id_ = ids[a], ids[b], ids[c], ids[d]
        ex, ey, ez = P[3 * ib] - P[3 * ia], P[3 * ib + 1] - P[3 * ia + 1], P[3 * ib + 2] - P[3 * ia + 2]
        el = math.sqrt(ex * ex + ey * ey + ez * ez)
        if el == 0:
            return 0.0
        ex, ey, ez = ex / el, ey / el, ez / el
        normals = []
        for t in (ic, id_):
            wx, wy, wz = P[3 * t] - P[3 * ia], P[3 * t + 1] - P[3 * ia + 1], P[3 * t + 2] - P[3 * ia + 2]
            k = wx * ex + wy * ey + wz * ez
            px, py, pz = wx - k * ex, wy - k * ey, wz - k * ez
            pl = math.sqrt(px * px + py * py + pz * pz)
            if pl == 0:
                return 0.0
            normals.append((px / pl, py / pl, pz / pl))
        c_ = max(-1.0, min(1.0, normals[0][0] * normals[1][0] + normals[0][1] * normals[1][1] + normals[0][2] * normals[1][2]))
        best = min(best, math.degrees(math.acos(c_)))
    return best


