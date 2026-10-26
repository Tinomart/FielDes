'''
Meshing: a body as a mesh of elements, for the solver of another program.

    from fieldes import *

    part = ...                                                   # any body, in mm
    mesh = mesh_volume(part, element_size=2.0)                   # tetrahedra of about 2 mm that follow the surface
    mesh = mesh_volume(part, 2.0, element='hex')                 # hexahedra (each tetrahedron cut into four)
    mesh = mesh_volume(part, 2.0, element='tet10')               # 10-node tetrahedra, the edges on the surface curved
    mesh = mesh_volume(part, graded_size(part, wall_size=0.5, growth_rate=1.2, max_size=4.0))      # 0.5 mm at the surface, growing inwards
    mesh = mesh_volume(air, 2.0, boundary_layers=inflation_layers(body, first_height=0.05, growth_rate=1.2, layers=6),
                       zones={'inlet': inlet, 'outlet': outlet})                # a flow: prism layers on the wall, named patches
    surface = mesh_surface(part, 1.0, element='quad')            # the surface alone, triangles or quadrilaterals

    print(mesh)                                                  # what it has
    print(mesh.quality().report())                               # skewness, orthogonal quality, aspect ratio ... and what they mean for a solver
    mesh.export('part.msh')                                      # Gmsh;  .vtu .inp .bdf .unv .stl ...;  format='fluent' | 'openfoam'

The mesh is made by isosurface stuffing on the body's own field (a conforming tetrahedral mesh whose boundary vertices lie on the surface: see the kernel's
tetmesh.hpp), and what is asked of it afterwards is done to that mesh:

    element_size    a number (mm), or a field -- a body whose value at a point is the element size there, a function of a list of points that returns their sizes,
                    or graded_size(...).  The mesh starts at the largest size the field has and is refined where it asks for less: the longest edge of a tetrahedron
                    is bisected (with the tetrahedra round it, so the mesh stays conforming) until its edges are no longer than the size asked for there, and the new
                    vertices of the boundary are put on the surface.  Without one: about forty elements along the longest side, no more than about a hundred thousand tetrahedra.
    element         'tet' (linear tetrahedra), 'tet10' (10-node tetrahedra), 'hex' (hexahedra: every tetrahedron cut into four -- the elements have the shape the
                    tetrahedra give them, which is not a hex-dominant mesh's, but they are hexahedra for a solver that needs them)
    boundary_layers inflation_layers(...): prism layers on the surface of a solid, growing into the fluid (the first height, the growth rate, the number of layers)
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

__all__ = ['mesh_volume', 'mesh_surface', 'graded_size', 'inflation_layers', 'MeshError']

# What the kinds of element are: the nodes they have, the nodes at their corners, and their faces -- each with the nodes in an order that has its normal (the
# right-hand rule) outward from the element.  The corners are numbered so that a tetrahedron has det(p1 - p0, p2 - p0, p3 - p0) > 0, a hexahedron has the
# nodes 0 1 2 3 round its bottom (seen from the top: counter-clockwise) and 4 5 6 7 above them, a prism has 0 1 2 round its bottom (counter-clockwise seen from
# the top) and 3 4 5 above them.  The ten nodes of a tetrahedron are its corners and the middle of the edges 0-1, 1-2, 2-0, 0-3, 1-3, 2-3
NODES = {'tri3': 3, 'quad4': 4, 'tet4': 4, 'tet10': 10, 'hex8': 8, 'prism6': 6}
CORNERS = {'tri3': 3, 'quad4': 4, 'tet4': 4, 'tet10': 4, 'hex8': 8, 'prism6': 6}
SOLIDS = ('tet4', 'tet10', 'prism6', 'hex8')
SURFACES = ('tri3', 'quad4')
FACES = {
    'tet4': ((0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)),
    'hex8': ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)),
    'prism6': ((0, 2, 1), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)),
}
FACES['tet10'] = FACES['tet4']
EDGES = {
    'tet4': ((0, 1), (1, 2), (2, 0), (0, 3), (1, 3), (2, 3)),
    'hex8': ((0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7)),
    'prism6': ((0, 1), (1, 2), (2, 0), (3, 4), (4, 5), (5, 3), (0, 3), (1, 4), (2, 5)),
}
EDGES['tet10'] = EDGES['tet4']
ELEMENTS = ('tet', 'tet10', 'hex')
_PAIRS = ((0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3))
# The hexahedron of the corner k of a tetrahedron: its three neighbours, in the order that makes the corner right-handed
_HEX_AT = ((1, 2, 3), (0, 3, 2), (0, 1, 3), (0, 2, 1))
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
        size has grown by 0.2 mm after one mm), up to `max_size`.  It is what mesh_volume's element_size takes for a mesh that is fine where the flow or the stress is
        and coarse elsewhere:

            mesh = mesh_volume(air, graded_size(body, wall_size=0.5, growth_rate=1.2, max_size=8))

        `wall` is a body: the distance is that from the surface of it, on either side (a solid in a fluid, or the fluid's own boundary) '''
    return _Graded(wall, wall_size, growth_rate, max_size)


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
    ''' Layers of prisms on the surface of a solid, for a flow: `layers` of them, the one at the wall `first_height` thick and each one `growth_rate` times as thick
        as the one before it (the first height is what sets y+: about y+ times the viscosity over the friction velocity).  mesh_volume's `boundary_layers` takes it:

            air = difference(box_of_air, body)
            mesh = mesh_volume(air, 2.0, boundary_layers=inflation_layers(body, first_height=0.05, growth_rate=1.2, layers=6))

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
        self.skewed = None              # (the cells above skewness 0.85, the cells above 0.95, the cells measured)
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
        shown = (('skewness', 'skewness (0 regular .. 1 degenerate)', _skew_word, 'max'),
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
            if key == 'skewness' and self.skewed and self.skewed[0]:
                over85, over95, measured = self.skewed
                lines.append('      {:,} of the {:,} cells ({:.2g} %) are above 0.85, the most that CFD solvers like, and {:,} above 0.95'.format(over85, measured, 100.0 * over85 / measured, over95))
        lines.extend('  ' + n for n in self.notes)
        return '\n'.join(lines)

    def __repr__(self):
        return self.report()


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
        self.cell_zone = {}
        self.cell_zone_names = ['fluid']
        self.faces = {}
        self.face_zone = {}
        self.zone_names = []
        self.element = 'tet'
        self.element_size = None
        self.notes = []
        self.seconds = 0.0
        self.body = None                # (the body it is the mesh of: what FielDes draws it on)
        self._shown = None

    # --- counts
    @property
    def node_count(self):
        return len(self.nodes) // 3

    def cell_count(self, kind=None):
        if kind is not None:
            return len(self.cells.get(kind, ())) // NODES[kind]
        return sum(len(c) // NODES[k] for k, c in self.cells.items())

    def face_count(self, kind=None, zone=None):
        total = 0
        for k, conn in self.faces.items():
            if kind is not None and k != kind:
                continue
            n = len(conn) // NODES[k]
            if zone is not None:
                z = self.zone_names.index(zone) if isinstance(zone, str) else zone
                total += sum(1 for v in self.face_zone[k] if v == z)
            else:
                total += n
        return total

    @property
    def bounds(self):
        n = self.node_count
        if not n:
            return ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
        P = self.nodes
        return (tuple(min(P[3 * i + a] for i in range(n)) for a in range(3)), tuple(max(P[3 * i + a] for i in range(n)) for a in range(3)))

    def cell_ids(self):
        ''' Every cell as (kind, index in its kind): the order the writers number them in '''
        for kind in SOLIDS:
            for i in range(self.cell_count(kind)):
                yield kind, i

    def corners(self, kind, i, faces=False):
        ''' The corner nodes of cell i (or of face i) of that kind '''
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
        return total

    def __repr__(self):
        parts = ['{:,} {}'.format(self.cell_count(k), k) for k in SOLIDS if self.cell_count(k)]
        faces = ['{:,} {}'.format(self.face_count(k), k) for k in SURFACES if self.face_count(k)]
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
        out.face_zone = {k: array('i', v) for k, v in self.face_zone.items()}
        out.zone_names = list(self.zone_names)
        out.element = self.element
        out.element_size = self.element_size
        out.body = self.body
        return out

    def _display(self):
        ''' What FielDes draws for a mesh stated on its own: the body, with a result card whose "Elements" show the cells, each coloured by its size, skewness, aspect ratio
            or orthogonal quality (a surface mesh: the body only) '''
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
    return (sum(P[3 * i] for i in ids) / n, sum(P[3 * i + 1] for i in ids) / n, sum(P[3 * i + 2] for i in ids) / n)


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

def _kernel_tets(field, lo, hi, h):
    ''' The kernel's conforming tetrahedra of the inside of a body: (nodes, tetrahedra) as flat arrays '''
    _need_kernel()
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i], hi[i]
    ptr = lib.libfive_tetmesh_new(field.ptr, region, float(h))
    if not ptr:
        message = lib.libfive_tetmesh_last_message()
        raise MeshError('the body cannot be meshed' + (': ' + message.decode('utf-8', 'replace') if message else ''))
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


def _smooth(P, tets, iterations, below=0.6):
    ''' The interior nodes of poor tetrahedra (a mean ratio below `below`) moved toward the middle of their neighbours -- the whole way or half of it, whichever makes the
        worst of their tetrahedra better, and not at all when neither does (smart Laplacian smoothing): a mesh that was refined by bisection has some poor tetrahedra, and
        this is what mends many of them '''
    boundary = _boundary_nodes(tets)
    around = {}
    neighbours = {}
    for ti, t in enumerate(tets):
        for v in t:
            if v not in boundary:
                around.setdefault(v, []).append(ti)
        for p, q in _PAIRS:
            a, b = t[p], t[q]
            if a not in boundary:
                neighbours.setdefault(a, set()).add(b)
            if b not in boundary:
                neighbours.setdefault(b, set()).add(a)
    nodes = sorted(around)
    quality = [_tet_ratio(P, t) for t in tets]
    for _ in range(iterations):
        moved = 0
        for n in nodes:
            ids = around[n]
            best = min(quality[i] for i in ids)
            if best >= below:
                continue
            near = neighbours[n]
            k = len(near)
            old = (P[3 * n], P[3 * n + 1], P[3 * n + 2])
            cx = sum(P[3 * m] for m in near) / k
            cy = sum(P[3 * m + 1] for m in near) / k
            cz = sum(P[3 * m + 2] for m in near) / k
            better = None
            for f in (1.0, 0.5):
                P[3 * n], P[3 * n + 1], P[3 * n + 2] = old[0] + f * (cx - old[0]), old[1] + f * (cy - old[1]), old[2] + f * (cz - old[2])
                values = [_tet_ratio(P, tets[i]) for i in ids]
                if min(values) > best + 1e-9:
                    best = min(values)
                    better = ((P[3 * n], P[3 * n + 1], P[3 * n + 2]), values)
            if better is None:
                P[3 * n], P[3 * n + 1], P[3 * n + 2] = old
            else:
                (P[3 * n], P[3 * n + 1], P[3 * n + 2]), values = better
                for i, value in zip(ids, values):
                    quality[i] = value
                moved += 1
        if not moved:
            break


def _smooth_boundary(P, tets, field, iterations=1, crease=40.0):
    ''' The nodes on the surface of poor tetrahedra moved along the surface: toward the middle of their neighbours on the surface, put back onto it with Newton steps on the
        field, and kept when that makes the worst of their tetrahedra better.  A node on a crease (where the faces round it turn by more than `crease` degrees) stays where it is '''
    cos_crease = math.cos(math.radians(crease))
    for _ in range(iterations):
        _, faces = _boundary_edges(tets)
        total = {}
        vectors = {}
        near = {}
        for f in faces:
            ax, ay, az = _polygon_area_vector(P, f)
            for v in f:
                s = total.setdefault(v, [0.0, 0.0, 0.0])
                s[0] += ax
                s[1] += ay
                s[2] += az
                vectors.setdefault(v, []).append((ax, ay, az))
            for i in range(3):
                a, b = f[i], f[(i + 1) % 3]
                near.setdefault(a, set()).add(b)
                near.setdefault(b, set()).add(a)
        quality = [_tet_ratio(P, t) for t in tets]
        around = {}
        for ti, t in enumerate(tets):
            for v in t:
                if v in total:
                    around.setdefault(v, []).append(ti)
        movable = []
        for v, s in total.items():
            length = math.sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2])
            if length <= 0.0 or min(quality[i] for i in around[v]) >= 0.6:
                continue
            n = (s[0] / length, s[1] / length, s[2] / length)
            flat = True
            for ax, ay, az in vectors[v]:
                a = math.sqrt(ax * ax + ay * ay + az * az)
                if a > 0.0 and (ax * n[0] + ay * n[1] + az * n[2]) / a < cos_crease:
                    flat = False
                    break
            if flat:
                movable.append((v, n))
        if not movable:
            return
        proposals = []
        scales = []
        for v, n in movable:
            ring = near[v]
            k = len(ring)
            dx = sum(P[3 * m] for m in ring) / k - P[3 * v]
            dy = sum(P[3 * m + 1] for m in ring) / k - P[3 * v + 1]
            dz = sum(P[3 * m + 2] for m in ring) / k - P[3 * v + 2]
            along = dx * n[0] + dy * n[1] + dz * n[2]
            proposals.append((P[3 * v] + dx - along * n[0], P[3 * v + 1] + dy - along * n[1], P[3 * v + 2] + dz - along * n[2]))
            scales.append(math.sqrt(dx * dx + dy * dy + dz * dz) + 1e-9)
        snapped = _snap_to_surface(field, proposals, scales)
        moved = 0
        for (v, n), q in zip(movable, snapped):
            ids = around[v]
            before = min(quality[i] for i in ids)
            old = (P[3 * v], P[3 * v + 1], P[3 * v + 2])
            P[3 * v], P[3 * v + 1], P[3 * v + 2] = q
            values = [_tet_ratio(P, tets[i]) for i in ids]
            if min(values) > before + 1e-9:
                for i, value in zip(ids, values):
                    quality[i] = value
                moved += 1
            else:
                P[3 * v], P[3 * v + 1], P[3 * v + 2] = old
        if not moved:
            return


def _optimize(P, tets, field, smooth, notes):
    ''' The poor tetrahedra mended in two rounds of: the nodes on the surface moved along it, the interior nodes moved, and edges and faces swapped.  Returns the tetrahedra '''
    swaps = 0
    for _ in range(2):
        if smooth:
            _smooth_boundary(P, tets, field)
            _smooth(P, tets, int(smooth))
        tets, done = _improve(P, tets)
        swaps += done
    if smooth:
        _smooth(P, tets, int(smooth))
    if swaps:
        notes.append('{:,} swaps of edges and faces, collapses of nodes, and moves of nodes mended the poor tetrahedra'.format(swaps))
    return tets


_FACES4 = ((0, 1, 2), (0, 1, 3), (0, 2, 3), (1, 2, 3))


def _improve(P, tets, passes=8, limit=0.6):
    ''' The tetrahedra of poor shape mended by swaps (Freitag and Ollivier-Gooch): an edge inside the mesh with a ring of n tetrahedra round it is taken out when the ring
        can be filled again in a way that makes the worst of them better (n = 3 is the 3-2 swap, n = 4 the 4-4 swap, and so on), a face inside the mesh is replaced by
        the three tetrahedra round the line between the apexes of the two that share it (the 2-3 swap), and a node inside the mesh is taken into a neighbour of its own
        (the edge collapse) when that makes the worst tetrahedra round it better.  The nodes do not move and the boundary stays as it is.
        A tetrahedron is poor when its mean ratio is below `limit`.  Returns the tetrahedra (a list of tuples) and the number of changes '''
    T = [tuple(t) for t in tets]
    boundary_edges, _ = _boundary_edges(T)
    surface = _boundary_nodes(T)
    edge_tets = {}
    face_tets = {}
    node_tets = {}

    def attach(i):
        t = T[i]
        for p, q in _PAIRS:
            edge_tets.setdefault(_edge(t[p], t[q]), set()).add(i)
        for f in _FACES4:
            face_tets.setdefault(tuple(sorted((t[f[0]], t[f[1]], t[f[2]]))), set()).add(i)
        for v in t:
            node_tets.setdefault(v, set()).add(i)

    def detach(i):
        t = T[i]
        for p, q in _PAIRS:
            key = _edge(t[p], t[q])
            edge_tets[key].discard(i)
            if not edge_tets[key]:
                del edge_tets[key]
        for f in _FACES4:
            key = tuple(sorted((t[f[0]], t[f[1]], t[f[2]])))
            face_tets[key].discard(i)
            if not face_tets[key]:
                del face_tets[key]
        for v in t:
            node_tets[v].discard(i)
            if not node_tets[v]:
                del node_tets[v]

    for i in range(len(T)):
        attach(i)

    def ratio(t):
        return _tet_ratio(P, t)

    def collapse(d, x):
        ''' (quality after, the new tetrahedra, the old ones) of the node d (not on the surface) taken into its neighbour x, or None.  It is allowed when the nodes that d and
            x have in common are the ones round the edge between them (so that no tetrahedra are pinched together) and no tetrahedron is left without a volume '''
        if d in surface or d not in node_tets:
            return None
        mine = node_tets[d]
        gone = [i for i in mine if x in T[i]]
        if not gone:
            return None
        ring = set()
        for i in gone:
            ring.update(T[i])
        ring.discard(d)
        ring.discard(x)
        near_d = set()
        for i in mine:
            near_d.update(T[i])
        near_x = set()
        for i in node_tets[x]:
            near_x.update(T[i])
        near_d.discard(d)
        near_d.discard(x)
        near_x.discard(d)
        near_x.discard(x)
        if near_d & near_x != ring:
            return None
        news = [tuple(x if v == d else v for v in T[i]) for i in mine if i not in gone]
        if not news:
            return None
        after = min(ratio(t) for t in news)
        if after <= min(ratio(T[i]) for i in mine) + 1e-9:
            return None
        return after, news, list(mine)

    def take_out_edge(e):
        ''' (quality after, the new tetrahedra, the old ones) of the best way to fill the ring round the edge, or None '''
        around = edge_tets.get(e)
        if not around or e in boundary_edges:
            return None
        n = len(around)
        if n < 3 or n > 8:
            return None
        a, b = e
        link = {}
        for i in around:
            c, d = [v for v in T[i] if v != a and v != b]
            if _det6(P, a, b, c, d) < 0.0:
                c, d = d, c
            if c in link:
                return None
            link[c] = d
        start = next(iter(link))
        ring = [start]
        v = link[start]
        while v != start:
            if v not in link or len(ring) > n:
                return None
            ring.append(v)
            v = link[v]
        if len(ring) != n:
            return None
        old = min(ratio(T[i]) for i in around)
        quality = {}

        def triangle(i, j, k):
            key = (i, j, k)
            if key not in quality:
                x, y, z = ring[i], ring[j], ring[k]
                quality[key] = min(ratio((a, x, y, z)), ratio((b, z, y, x)))
            return quality[key]

        best = {}

        def fill(i, j):
            ''' (quality of the best triangulation of the ring from i to j, the apex k of the triangle on the edge i-j) '''
            if j - i < 2:
                return 2.0, None
            if (i, j) in best:
                return best[(i, j)]
            if j - i > 1 and not (i == 0 and j == n - 1) and _edge(ring[i], ring[j]) in edge_tets:
                best[(i, j)] = (-1.0, None)
                return best[(i, j)]
            top = (-1.0, None)
            for k in range(i + 1, j):
                q = min(triangle(i, k, j), fill(i, k)[0], fill(k, j)[0])
                if q > top[0]:
                    top = (q, k)
            best[(i, j)] = top
            return top

        quality_after, _ = fill(0, n - 1)
        if quality_after <= old + 1e-9 or quality_after <= 0.0:
            return None
        news = []
        stack = [(0, n - 1)]
        while stack:
            i, j = stack.pop()
            if j - i < 2:
                continue
            k = fill(i, j)[1]
            x, y, z = ring[i], ring[k], ring[j]
            news.append((a, x, y, z))
            news.append((b, z, y, x))
            stack.append((i, k))
            stack.append((k, j))
        return quality_after, news, list(around)

    def swap_face(key):
        owners = face_tets.get(key)
        if not owners or len(owners) != 2:
            return None
        i, j = tuple(owners)
        a, b, c = key
        d = [v for v in T[i] if v not in key][0]
        e = [v for v in T[j] if v not in key][0]
        if _edge(d, e) in edge_tets:
            return None
        volumes = [_det6(P, d, e, x, y) for x, y in ((a, b), (b, c), (c, a))]
        if not (all(v > 0.0 for v in volumes) or all(v < 0.0 for v in volumes)):
            return None
        news = []
        for (x, y), v in zip(((a, b), (b, c), (c, a)), volumes):
            news.append((d, e, x, y) if v > 0.0 else (d, e, y, x))
        after = min(ratio(t) for t in news)
        if after <= min(ratio(T[i]), ratio(T[j])) + 1e-9:
            return None
        return after, news, [i, j]

    swaps = 0
    touched = None              # (after the first pass only the poor tetrahedra at a swap are tried again: the others have the same neighbours as before)
    for _ in range(passes):
        poor = sorted((ratio(t), i) for i, t in enumerate(T) if t is not None and (touched is None or any(v in touched for v in t)) and ratio(t) < limit)
        touched = set()
        changed = 0
        for _, i in poor:
            if T[i] is None:
                continue
            t = T[i]
            if ratio(t) >= limit:
                continue
            best_swap = None
            for p, q in _PAIRS:
                r = take_out_edge(_edge(t[p], t[q]))
                if r is not None and (best_swap is None or r[0] > best_swap[0]):
                    best_swap = r
            for f in _FACES4:
                r = swap_face(tuple(sorted((t[f[0]], t[f[1]], t[f[2]]))))
                if r is not None and (best_swap is None or r[0] > best_swap[0]):
                    best_swap = r
            for d in t:
                if d in surface:
                    continue
                for x in t:
                    if x != d:
                        r = collapse(d, x)
                        if r is not None and (best_swap is None or r[0] > best_swap[0]):
                            best_swap = r
            if best_swap is None:
                continue
            for u in best_swap[2]:
                touched.update(T[u])
                detach(u)
                T[u] = None
            for nt in best_swap[1]:
                T.append(nt)
                attach(len(T) - 1)
            changed += 1
        swaps += changed
        if not changed:
            break
    return [t for t in T if t is not None], swaps


def _boundary_edges(tets):
    ''' The edges of the faces that only one tetrahedron has, and those faces '''
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


def _to_tet10(P, tets, field, curved):
    ''' 10-node tetrahedra: a node in the middle of every edge, on the surface when the edge is (and `curved`) '''
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
        target = _snap_to_surface(field, points, scales, 2)
        for (m, e), p in zip(new_boundary, target):
            P[3 * m], P[3 * m + 1], P[3 * m + 2] = p
    return out


def _to_hex(P, tets, field, curved):
    ''' Hexahedra: every tetrahedron cut into four, one at each of its corners, with the nodes of the middle of its edges, of its faces and of itself.  The nodes
        of the boundary -- an edge's middle, a face's -- go to the surface when `curved` '''
    boundary_edges, boundary_faces = _boundary_edges(tets) if curved else (set(), [])
    boundary_face_keys = set(tuple(sorted(f)) for f in boundary_faces)
    edge_node = {}
    face_node = {}
    out = array('i')
    on_surface = []

    def edge_mid(a, b):
        e = _edge(a, b)
        m = edge_node.get(e)
        if m is None:
            m = len(P) // 3
            edge_node[e] = m
            P.extend(((P[3 * a] + P[3 * b]) * 0.5, (P[3 * a + 1] + P[3 * b + 1]) * 0.5, (P[3 * a + 2] + P[3 * b + 2]) * 0.5))
            if e in boundary_edges:
                on_surface.append(m)
        return m

    def face_mid(a, b, c):
        k = tuple(sorted((a, b, c)))
        m = face_node.get(k)
        if m is None:
            m = len(P) // 3
            face_node[k] = m
            P.extend(((P[3 * a] + P[3 * b] + P[3 * c]) / 3.0, (P[3 * a + 1] + P[3 * b + 1] + P[3 * c + 1]) / 3.0, (P[3 * a + 2] + P[3 * b + 2] + P[3 * c + 2]) / 3.0))
            if k in boundary_face_keys:
                on_surface.append(m)
        return m

    for t in tets:
        v = t
        centre = len(P) // 3
        P.extend(tuple(sum(P[3 * x + a] for x in v) * 0.25 for a in range(3)))
        for k in range(4):
            a, b, c = _HEX_AT[k]
            vk, va, vb, vc = v[k], v[a], v[b], v[c]
            out.extend((vk, edge_mid(vk, va), face_mid(vk, va, vb), edge_mid(vk, vb), edge_mid(vk, vc), face_mid(vk, va, vc), centre, face_mid(vk, vb, vc)))
    if on_surface:
        points = [(P[3 * m], P[3 * m + 1], P[3 * m + 2]) for m in on_surface]
        # (the size of a step: half an edge of the tetrahedra at most -- the mean edge of the mesh's first ones)
        sample = tets[:200]
        edges = [math.sqrt((P[3 * t[i]] - P[3 * t[j]]) ** 2 + (P[3 * t[i] + 1] - P[3 * t[j] + 1]) ** 2 + (P[3 * t[i] + 2] - P[3 * t[j] + 2]) ** 2) for t in sample for i, j in _PAIRS]
        scale = 0.5 * sum(edges) / len(edges)
        target = _snap_to_surface(field, points, [scale] * len(points), 2)
        for m, p in zip(on_surface, target):
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
    out = {'tri3': array('i'), 'quad4': array('i')}
    for key in order:
        nodes = seen[key]
        if nodes is None:
            continue
        out['tri3' if len(nodes) == 3 else 'quad4'].extend(nodes)
    return {k: v for k, v in out.items() if len(v)}


def _face_centres(mesh, kind):
    P = mesh.nodes
    n = NODES[kind]
    conn = mesh.faces[kind]
    out = []
    for i in range(len(conn) // n):
        out.append(_centroid(P, conn[n * i:n * i + n]))
    return out


def _face_normals(mesh, kind):
    P = mesh.nodes
    n = NODES[kind]
    conn = mesh.faces[kind]
    out = []
    for i in range(len(conn) // n):
        ax, ay, az = _polygon_area_vector(P, conn[n * i:n * i + n])
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
        n = NODES[kind]
        conn = mesh.faces[kind]
        normals = _face_normals(mesh, kind)
        for i in range(len(conn) // n):
            if mesh.face_zone[kind][i] != wall_index:
                continue
            fid = len(faces)
            faces.append((kind, i, normals[i][:3], normals[i][3]))
            ids = conn[n * i:n * i + n]
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
    mesh.cell_zone_names = names


def mesh_volume(body, element_size=None, element='tet', boundary_layers=None, zones=None, default_zone='wall', feature_angle=None, cell_zones=None,
                curved=True, smooth=6, optimize=True, bounds=None, name=None):
    ''' The inside of `body` as a mesh of elements (see the description of this module).

            mesh = mesh_volume(part, element_size=2.0)

        element_size    the element edge in mm, or a field (see graded_size); default: about 40 elements along the longest side, no more than about a hundred thousand tetrahedra
        element         'tet', 'tet10' or 'hex'
        boundary_layers inflation_layers(...): prisms on the surface of a solid
        zones           {name: region}: the patches of the boundary; the rest are `default_zone`; feature_angle (degrees) splits those at the creases
        cell_zones      {name: region}: zones of cells; the rest are 'fluid'
        curved          the nodes the elements have in the middle of edges and faces on the boundary (tet10, hex) lie on the surface
        smooth          rounds of moving the nodes of poor tetrahedra toward the middle of their neighbours (along the surface for the nodes on it), where that makes the
                        worst of their tetrahedra better; 0 for none
        optimize        mend the poor tetrahedra: twice the nodes are moved as `smooth` says and edges and faces are swapped (False: the mesh as it comes, the fastest)
        bounds         ((x0, y0, z0), (x1, y1, z1)): the box to mesh in, when the body's cannot be found '''
    t0 = time.time()
    if not isinstance(body, Shape):
        raise TypeError('mesh_volume: the body is a Shape, not {!r}'.format(type(body).__name__))
    if element not in ELEMENTS:
        raise ValueError("mesh_volume: element is 'tet', 'tet10' or 'hex'")
    from fieldes.stdlib.fields import _shape_bounds
    lo, hi = _shape_bounds(body, bounds)
    extent = max(float(hi[i]) - float(lo[i]) for i in range(3))
    if not extent > 0:
        raise MeshError('mesh_volume: the body has no size')
    pad = 1e-6 * extent
    box = (tuple(float(c) - pad for c in lo), tuple(float(c) + pad for c in hi))
    notes = []
    layers = None
    if boundary_layers is not None:
        if not isinstance(boundary_layers, _Inflation):
            raise TypeError('mesh_volume: boundary_layers is inflation_layers(...)')
        layers = boundary_layers
    if element_size is None:
        element_size = default_element_size(body, box[0], box[1], 40.0, DEFAULT_ELEMENTS)
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
    P, T = _kernel_tets(core, box[0], box[1], base)
    if not len(T):
        raise MeshError('mesh_volume: the body has nothing inside the box it is meshed in')
    if layers is not None and element != 'tet':
        raise MeshError("mesh_volume: boundary_layers are made with element='tet' (the prisms and the elements of the core share their faces: triangles)")
    if sizer.uniform is None:
        tet_list = _refine_to_size(P, T, sizer, core, notes=notes)
    else:
        tet_list = [tuple(T[4 * i:4 * i + 4]) for i in range(len(T) // 4)]
    if optimize:
        tet_list = _optimize(P, tet_list, core, smooth, notes)
    elif smooth and sizer.uniform is None:
        _smooth(P, tet_list, int(smooth))
    T = array('i')
    for t in tet_list:
        T.extend(t)
    mesh = Mesh(name or 'mesh')
    mesh.element = element
    mesh.element_size = sizer.uniform if sizer.uniform is not None else base
    mesh.notes = notes
    mesh.body = body
    mesh.nodes = P
    if element == 'tet':
        mesh.cells['tet4'] = T
        if layers is not None:
            _add_layers(mesh, tet_list or [tuple(T[4 * i:4 * i + 4]) for i in range(len(T) // 4)], wall_distance, layers, notes)
    else:
        tet_list = tet_list or [tuple(T[4 * i:4 * i + 4]) for i in range(len(T) // 4)]
        mesh.cells['tet10' if element == 'tet10' else 'hex8'] = (_to_tet10 if element == 'tet10' else _to_hex)(P, tet_list, core, curved)
    _compact_nodes(mesh)
    mesh.faces = _boundary_of(mesh)
    tolerance = 0.02 * (mesh.element_size or base)
    _assign_zones(mesh, zones, default_zone, feature_angle, tolerance)
    _assign_cell_zones(mesh, cell_zones, tolerance)
    mesh.seconds = time.time() - t0
    return mesh


def mesh_surface(body, element_size=None, element='tri', zones=None, default_zone='wall', feature_angle=None, bounds=None, name=None):
    ''' The surface of `body` as a mesh of triangles ('tri') or quadrilaterals ('quad': every triangle cut into three, so the quadrilaterals are about half the
        size of the triangles).  It is the boundary of the tetrahedra that mesh_volume makes, so it follows the body's surface and is conforming, and it costs the
        volume mesh; its arguments are mesh_volume's '''
    if element not in ('tri', 'quad'):
        raise ValueError("mesh_surface: element is 'tri' or 'quad'")
    mesh = mesh_volume(body, element_size, 'tet', zones=zones, default_zone=default_zone, feature_angle=feature_angle, bounds=bounds, name=name or 'surface')
    out = mesh.surface_mesh()
    if element == 'quad':
        _tris_to_quads(out)
    out.element = element
    return out


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
}
MAX_DISPLAYED = 400000          # (tetrahedra the viewer is given: the cells of a larger mesh are not drawn)


def _display_tets(mesh):
    ''' Every cell as tetrahedra, right-handed: (the tetrahedra as a flat array, the cell of each, the first tetrahedron of every cell and one past the last) '''
    P = mesh.nodes
    tets = array('i')
    owner = array('i')
    first = []
    gid = 0
    for kind in SOLIDS:
        conn = mesh.cells.get(kind)
        if not conn:
            continue
        n = NODES[kind]
        for i in range(len(conn) // n):
            ids = conn[n * i:n * i + n]
            first.append(len(tets) // 4)
            parts = [tuple(ids[:4])] if kind in ('tet4', 'tet10') else [tuple(ids[j] for j in split) for split in _TET_SPLIT[kind]]
            for t in parts:
                if _det6(P, t[0], t[1], t[2], t[3]) < 0.0:
                    t = (t[1], t[0], t[2], t[3])
                tets.extend(t)
                owner.append(gid)
            gid += 1
    first.append(len(tets) // 4)
    return tets, owner, first


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
        tets, owner, first = _display_tets(mesh)
        ntets = len(tets) // 4
        if ntets > MAX_DISPLAYED:
            note = '{:,} cells: more than the {:,} that are drawn, so the elements are not shown'.format(ntets, MAX_DISPLAYED)
        else:
            for name, label, data, lo, hi, mean in (extra or ()):
                constant = body * 0.0 + float(mean)
                channels.append((name, label, field_shape(_lib.libfive_tree_copy(constant.ptr)), lo, hi))
                values.append(data)
            q = _measure(mesh, False, 150000, collect=True)
            labels = (('size', 'Element size (mm)'), ('skewness', 'Skewness (0 regular, 1 degenerate)'), ('aspect_ratio', 'Aspect ratio'),
                      ('orthogonal_quality', 'Orthogonal quality (1 best)'), ('volume_ratio', 'Volume ratio to the neighbours'))
            for name, label in labels:
                per = q.per_cell.get(name)
                if not per:
                    continue
                lo, hi = min(per.values()), max(per.values())
                if name == 'aspect_ratio' or name == 'volume_ratio':
                    hi = min(hi, 100.0)
                if not hi > lo:
                    hi = lo + 1.0
                mean = sum(per.values()) / len(per)
                constant = body * 0.0 + float(mean)
                channels.append((name, label + ' - per element, press Elements', field_shape(_lib.libfive_tree_copy(constant.ptr)), lo, hi))
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
            faces = array('i')
            face_tet = array('i')
            for kind, conn in mesh.faces.items():
                n = NODES[kind]
                for i in range(len(conn) // n):
                    fn = tuple(conn[n * i:n * i + n])
                    c = cell_of.get(tuple(sorted(fn)))
                    if c is None:
                        continue
                    for tri in ((fn,) if n == 3 else ((fn[0], fn[1], fn[2]), (fn[0], fn[2], fn[3]))):
                        best, most = first[c], -1
                        for tt in range(first[c], first[c + 1]):
                            have = sum(1 for v in tri if v in tets[4 * tt:4 * tt + 4])
                            if have > most:
                                best, most = tt, have
                        faces.extend(tri)
                        face_tet.append(best)
            verts = array('f', P)
            fea_mesh = (verts.tobytes(), tets.tobytes(), faces.tobytes(), face_tet.tobytes(),
                        '{:,} cells · {:,} nodes'.format(cells, mesh.node_count), values, array('f', [0.0] * (3 * mesh.node_count)).tobytes())
    if not channels:
        # (a surface mesh, or one that is not drawn: the body, with a size that is the one the mesh has)
        size = float(mesh.element_size or 1.0)
        constant = body * 0.0 + size
        channels.append(('size', 'Element size (mm)', field_shape(_lib.libfive_tree_copy(constant.ptr)), size * 0.5, size * 1.5))
    name, label, field, lo, hi = channels[0]
    out = colored(body, field, range=(lo, hi), label=label, colormap='turbo')
    out._color_fields = channels
    out._color_field_name = name
    out._fea_mesh = fea_mesh
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


def _measure(mesh, full, max_cells, collect=False):
    ''' The quality of the mesh (see Mesh.quality).  `collect` keeps the value of every cell, in the order of cell_ids(), as q.per_cell[name] -- what the viewer
        colours the elements by '''
    t0 = time.time()
    q = MeshQuality()
    P = mesh.nodes
    total = mesh.cell_count()
    q.counts = {k: mesh.cell_count(k) for k in SOLIDS}
    stride = 1
    neighbours = full or total <= max_cells
    per = {'size': {}, 'skewness': {}, 'aspect_ratio': {}}
    if not neighbours and not collect:
        stride = max(2, total // max_cells)
        q.sampled = stride
        q.notes.append('The mesh has {:,} cells: orthogonal quality and volume ratio need the faces of all of them and are not measured (quality(full=True) measures them)'.format(total))
    skew, aspect, dihedral = [], [], []
    centroid = {}
    volume = {}
    faces = {}                  # key -> [cell, outward area vector, centre of the face, (second cell)]
    gid = 0
    cell_orth = {}
    for kind in SOLIDS:
        conn = mesh.cells.get(kind)
        if not conn:
            continue
        n = NODES[kind]
        k = CORNERS[kind]
        for i in range(len(conn) // n):
            ids = conn[n * i:n * i + k]
            my = gid
            gid += 1
            v = _cell_volume(P, kind, ids)
            q.volume += v
            if v <= 0.0:
                q.negative += 1
            if not neighbours and (i % stride):
                continue
            cx, cy, cz = _centroid(P, ids)
            centroid[my] = (cx, cy, cz)
            volume[my] = v
            s, a_, mean_edge, dih = _cell_metrics(P, kind, ids, v)
            skew.append(s)
            aspect.append(a_)
            if dih is not None:
                dihedral.append(dih)
            if collect:
                per['size'][my] = mean_edge
                per['skewness'][my] = s
                per['aspect_ratio'][my] = a_
            if neighbours:
                for face in FACES[kind]:
                    fid = [ids[j] for j in face]
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
    q.metrics['aspect_ratio'] = _stats(aspect)
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
    q.seconds = time.time() - t0
    return q


def _stats(values):
    if not values:
        return (0.0, 0.0, 0.0)
    return (min(values), sum(values) / len(values), max(values))


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


# (what the model tree and the completion write for a mesh is `mesh_volume(body)`: everything else has a default that does the usual thing -- the size from the geometry, tetrahedra,
# the layers and zones only when they are asked for -- and nothing of it needs to be in front of somebody who wants to see a mesh and write it)
mesh_volume._short_call = True
mesh_surface._short_call = True
