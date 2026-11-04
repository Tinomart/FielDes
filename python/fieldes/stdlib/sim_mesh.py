'''
A mesh made with the meshing operations, as the part of an analysis.

    mesh = mesh_volume(bracket, graded_size(bracket, wall_size=0.5, growth_rate=1.3, max_size=4.0))        # fine at the surface, coarse inside
    result = static_analysis(mesh, supports=[fixed(base)], loads=[force(lug, (0, 0, -300))], material=steel)

An analysis that is given a body makes its own mesh of it: one size everywhere (`element_size`), tetrahedra cut from a lattice laid over the body.  Given the mesh of a
body instead -- the result of mesh_volume -- it solves on exactly those elements: a size that follows the surface or the stress (graded_size, a field), a mesh that was
improved, a 10-node mesh (its corners are taken).  The body of the mesh is the part everything else is about: the result is drawn on it, its bounds are found from the
mesh, and the supports and loads are regions as ever (the nodes inside a region are held, the faces inside it are loaded).

static_analysis, modal_analysis and thermal_analysis take the mesh in place of the part; fluid_analysis takes the mesh of the fluid in place of the domain (the mesh of the
domain with the body cut out: mesh_volume(difference(domain, body), ...)).  What is taken from the mesh is its tetrahedra ('tet4', or the corners of 'tet10' cells): the
solvers of FielDes work on tetrahedra, so a mesh of hexahedra or with the prism layers of a boundary layer is refused, and says why.  The size of the elements is the
mesh's own: `element_size` beside a mesh is an error, as is element='hex' (a voxel grid of its own).  element='tet10' gives the quadratic element on the corners of the mesh.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import hashlib
import math
from array import array

from fieldes.shape import Shape

__all__ = []

SOLID_KINDS = ('tet4', 'tet10')


def _error(text):
    from fieldes.stdlib.fea import FeaError
    return FeaError(text)


def is_mesh(value):
    from fieldes.stdlib import meshing
    return isinstance(value, meshing.Mesh)


class Given(object):
    ''' A mesh as the kernel takes it: the nodes (x y z), the corners of the tetrahedra (four each), what identifies it for the caches, its box and the size of its elements '''

    def __init__(self, mesh, nodes, tets):
        self.mesh = mesh
        self.nodes = nodes
        self.tets = tets
        h = hashlib.md5()
        h.update(nodes.tobytes())
        h.update(tets.tobytes())
        self.digest = h.hexdigest()
        self.bounds = (tuple(min(nodes[a::3]) for a in range(3)), tuple(max(nodes[a::3]) for a in range(3)))
        self.size = _middle_edge(nodes, tets)


def _middle_edge(P, tets):
    ''' The middle one of the edge lengths of a sample of the tetrahedra: the size of an element (a graded mesh has all sizes between its finest and its coarsest) '''
    count = len(tets) // 4
    lengths = []
    for e in range(0, count, max(1, count // 4000)):
        ids = tets[4 * e:4 * e + 4]
        for a in range(3):
            for b in range(a + 1, 4):
                i, j = 3 * ids[a], 3 * ids[b]
                lengths.append(math.sqrt((P[i] - P[j]) ** 2 + (P[i + 1] - P[j + 1]) ** 2 + (P[i + 2] - P[j + 2]) ** 2))
    lengths.sort()
    return lengths[len(lengths) // 2] if lengths else 1.0


def _tetrahedra(mesh, what):
    kinds = [k for k, c in mesh.cells.items() if len(c)]
    if not kinds:
        raise _error('%s: the mesh has no cells -- a surface mesh (mesh_surface) has no volume to solve on: give the mesh of the volume, mesh_volume(body, ...)' % what)
    other = [k for k in kinds if k not in SOLID_KINDS]
    if other:
        names = {'hex8': 'hexahedra', 'prism6': 'prisms'}
        raise _error("%s: the mesh has %s, and the solvers of FielDes work on tetrahedra -- make the mesh with mesh_volume(body, ..., element='tet') or 'tet10' (and no "
                     "boundary_layers: their prisms are for the solver of another program)" % (what, ' and '.join(names.get(k, k) for k in other)))
    tets = array('i')
    if len(mesh.cells.get('tet4', ())):
        tets.extend(mesh.cells['tet4'])
    ten = mesh.cells.get('tet10', ())
    for i in range(0, len(ten), 10):
        tets.extend(ten[i:i + 4])
    return tets


def part(value, what, element_size, element):
    ''' (the body, the mesh as it is handed to the kernel) of what an analysis was given as its part.  A Mesh is the mesh of a body; anything else is handed back as it
        is, with no mesh, for the analysis to take as it always did '''
    if not is_mesh(value):
        return value, None
    if element_size is not None:
        raise _error('%s: element_size has no meaning with a mesh -- the mesh is the elements (make it finer or coarser with mesh_volume(body, element_size))' % what)
    if element not in ('tet', 'tet10'):
        raise _error("%s: element=%r is a voxel grid of its own and takes no mesh -- a mesh is solved with element='tet' (the default) or 'tet10'" % (what, element))
    if not isinstance(value.body, Shape):
        raise _error('%s: the mesh is the mesh of no body -- the analysis draws its result on the body: make it with mesh_volume(body, ...)' % what)
    tets = _tetrahedra(value, what)
    if array('i').itemsize != 4:
        raise _error('%s: this computer has integers of another size than the kernel reads' % what)
    return value.body, Given(value, value.nodes, tets)


def key(given):
    ''' What a mesh adds to the key of a problem (nothing for an analysis of a body: its keys are as they were) '''
    return {} if given is None else {'given_mesh': given.digest}         # ('mesh' is taken: the flow's key has its meshing version under it)


def give(kind, ptr, given, what):
    ''' Hands the mesh to the kernel's problem (kind 'tetfea', 'tetthermal' or 'tetflow') in place of the mesh it would make of the body '''
    from fieldes.ffi import lib
    fn = getattr(lib, 'libfive_%s_set_mesh' % kind, None)
    if fn is None:
        raise _error('%s: this FielDes library is too old for a mesh as the part' % what)
    nodes = (ctypes.c_double * len(given.nodes)).from_buffer(given.nodes)
    tets = (ctypes.c_int32 * len(given.tets)).from_buffer(given.tets)
    if not fn(ptr, len(given.nodes) // 3, nodes, len(given.tets) // 4, tets):
        message = getattr(lib, 'libfive_%s_message' % kind, None)
        text = message(ptr).decode('utf-8', 'replace') if message else 'the kernel refused it'
        raise _error('%s: the mesh cannot be used: %s' % (what, text))


def takes_mesh(fn):
    ''' Says of an analysis that it can be given the mesh of a body in place of its part: the menus read it (menu_catalog._part_kinds), so that a selected mesh row is offered the analysis '''
    fn._takes_mesh = True
    return fn
