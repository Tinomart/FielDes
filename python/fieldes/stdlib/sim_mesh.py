'''
A mesh made with the meshing operations, as the part of an analysis.

    mesh = mesh_tet4(bracket, graded_size(bracket, wall_size=0.5, growth_rate=1.3, max_size=4.0))        # fine at the surface, coarse inside
    result = static_analysis(mesh, supports=[fixed(base)], loads=[force(lug, (0, 0, -300))], material=steel)

An analysis that is given a body makes its own mesh of it: one size everywhere (`element_size`), tetrahedra cut from a lattice laid over the body and mended by the
kernel's passes of Netgen's volume optimization (kernel/src/fea/tetmender.cpp).  Given the mesh of a
body instead -- the result of mesh_tet4 -- it solves on exactly those elements: a size that follows the surface or the stress (graded_size, a field), a mesh that was
improved, a 10-node mesh (its corners are taken).  The body of the mesh is the part everything else is about: the result is drawn on it, its bounds are found from the
mesh, and the supports and loads are regions as ever (the nodes inside a region are held, the faces inside it are loaded).

static_analysis, modal_analysis and thermal_analysis take the mesh in place of the part; fluid_analysis takes the mesh of the fluid in place of the domain (the mesh of the
domain with the body cut out: mesh_tet4(difference(domain, body), ...)).  What is taken from the mesh is its tetrahedra ('tet4', or the corners of 'tet10' cells): the
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
        raise _error('%s: the mesh has no cells -- a surface mesh (mesh_surface_tri) has no volume to solve on: give the mesh of the volume, mesh_tet4(body, ...)' % what)
    other = [k for k in kinds if k not in SOLID_KINDS]
    if other:
        names = {'hex8': 'hexahedra', 'prism6': 'prisms'}
        raise _error("%s: the mesh has %s, and the solvers of FielDes work on tetrahedra -- make the mesh with mesh_tet4(body, ...) or mesh_tet10(body, ...) (and no "
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
        raise _error('%s: element_size has no meaning with a mesh -- the mesh is the elements (make it finer or coarser with mesh_tet4(body, element_size))' % what)
    if element not in ('tet', 'tet10'):
        raise _error("%s: element=%r is a voxel grid of its own and takes no mesh -- a mesh is solved with element='tet' (the default) or 'tet10'" % (what, element))
    if not isinstance(value.body, Shape):
        raise _error('%s: the mesh is the mesh of no body -- the analysis draws its result on the body: make it with mesh_tet4(body, ...)' % what)
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


# H225 (the user, 2026-11-15: "The threshold of when elements are removed ... sound like something that should be an argument that the user can choose to have an active choice between less odd elements or more
# surface conformity"): the argument `surface_fit` of the meshing and analysis functions.  A mesh cut from a lattice has odd elements near the surface, and the mender decides which of them go: a tetrahedron whose
# mean ratio is under `poor` is swapped, collapsed or split, and a collapse of the skin is allowed while the surface stays within a tolerance (a fraction of the element size).  Measured on the real bracket at 3 mm
# (python tests/internal/surface_fit_checks.py and the probes of the worklist line H225): the higher the threshold, the better the bulk of the mesh (the tetrahedra at the 0.1 percentile of the mean ratio 0.52, 0.60, 0.64 for
# a threshold of 0.5, 0.6, 0.7), the coarser the skin (26.0, 25.4 and 24.0 thousand boundary faces) and the longer the mesh takes (6.2, 6.6 and 7.2 s).  The edge pass has two limits of its own -- the poorest tetrahedron
# and the smallest dihedral angle that a move of a node onto an edge may leave -- which trade the number of faces that lean away from the surface for the worst tetrahedron.  The number of the
# argument sets the four numbers together; the kernel holds them while a mesh is made.  (floor, sliver degrees, surface tolerance, mender poor)
# H225, SECOND VERSION (the user, 2026-11-15, capitals: "I ASKED FOR AN ARGUEMTN THAT LETS THE FUCKING USER DETERMINE THIS SO WE DONT SET IT ARBITRARILY"; the first version had three WORDS with numbers of mine): the argument is
# a NUMBER from 0 to 1 that the user types, 0.5 the default (what the mesher has always done).  Towards 0 the mesher keeps fewer odd elements and lets the skin stand farther from the exact surface; towards 1 it keeps
# the surface and lets odd elements stay.  The number sets the four limits together, by straight lines between the three values they have at 0, 0.5 and 1 (the table is in docs/analysis.md):
#                               at 0     at 0.5 (default)   at 1
#   floor   (the poorest tetrahedron a move of a node onto an edge may leave, mean ratio)   0.15     0.15      0.03
#   sliver  (the smallest dihedral angle such a move may leave, degrees)                    10       10        2
#   skin    (how far the skin may stand off the exact surface, a fraction of the element)  0.12     0.06      0.02
#   poor    (the mender works on every tetrahedron under this mean ratio)                  0.70     0.60      0.50
FIT_DEFAULT = 0.5
_FIT_ANCHORS = ((0.15, 10.0, 0.12, 0.70), (0.15, 10.0, 0.06, 0.60), (0.03, 2.0, 0.02, 0.50))        # (floor, sliver degrees, surface tolerance, mender poor) at 0, 0.5 and 1

_surface_fit_stack = [FIT_DEFAULT]


def fit_limits(fit):
    ''' The four limits (floor, sliver degrees, surface tolerance, mender poor) that the number `fit` stands for: straight lines between the values at 0, 0.5 and 1 '''
    t = float(fit)
    if t <= 0.5:
        a, b, s = _FIT_ANCHORS[0], _FIT_ANCHORS[1], t / 0.5
    else:
        a, b, s = _FIT_ANCHORS[1], _FIT_ANCHORS[2], (t - 0.5) / 0.5
    return tuple(b[k] if s >= 1.0 else a[k] + (b[k] - a[k]) * s for k in range(4))          # (an anchor is exactly itself)


def surface_fit_value(value, what):
    ''' The number, checked: None is the default, 0.5; a number from 0 to 1 is itself '''
    if value is None:
        return FIT_DEFAULT
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not (0.0 <= float(value) <= 1.0):
        raise ValueError("%s: surface_fit is a number from 0 (fewest odd elements) to 1 (most surface conformity), 0.5 by default, not %r" % (what, value))
    return float(value)


def check_fit_with_mesh(given, fit, what):
    ''' A mesh that is given is the elements: how a mesh is made has no meaning beside it (as element_size has none) '''
    if given is not None and fit != FIT_DEFAULT:
        raise _error('%s: surface_fit has no meaning with a mesh -- the mesh is the elements (make it with the number: mesh_tet4(body, surface_fit=0.8))' % what)


def fit_key(fit):
    ''' What the number adds to the key of a problem: nothing for the default (the keys, and the results kept under them, are as they were) '''
    return {} if fit == FIT_DEFAULT else {'surface_fit': fit}


def current_surface_fit():
    ''' The number that stands now (it goes into the key of a result: the same part meshed with another number is another mesh) '''
    return _surface_fit_stack[-1]


def _set_limits(fit):
    from fieldes.ffi import lib
    fn = getattr(lib, 'libfive_tetmesh_set_limits', None)
    if fn is None:
        if fit != FIT_DEFAULT:
            raise _error('this FielDes library is too old for surface_fit')
        return
    fn(*fit_limits(fit))


# H226 (the user, 2026-11-16: "this should certainly be an argument for each meshing that can turn it off. But I would definitely have it on by default"): the argument `symmetry` of the meshing and analysis functions.  A part that is
# symmetric about a plane through the middle of its box is meshed as the mirror image of a half, so that its mesh is symmetric too (a lattice cut by a shape cannot be: the cut of a square goes along the diagonal through its
# lowest-numbered vertex, and a mirror reverses the numbers).  The decision is made in the kernel, in the one function every tetrahedral mesh passes through (meshShape); this is what the argument says to it, as the bit set of
# the planes it may mirror about.  The words are those of the symmetry of topology_optimization: 'auto' (the default), None, and the axes
SYMMETRY_ALL = 7
_AXIS_BITS = {'x': 1, 'y': 2, 'z': 4}
_symmetry_stack = [SYMMETRY_ALL]


def symmetry_value(value, what):
    ''' The planes a symmetric part may be mirrored about, as a bit set (1 x, 2 y, 4 z), from the argument: 'auto' every plane (the default), None none (the part is meshed whole), 'x', 'y', 'z' or several of them ('xz', ['x', 'z']) '''
    if value is None:
        return 0
    letters = None
    if isinstance(value, str):
        text = value.strip().lower()
        if text == 'auto':
            return SYMMETRY_ALL
        letters = list(text)
    elif isinstance(value, (list, tuple)) and value and all(isinstance(c, str) for c in value):
        letters = [c.strip().lower() for c in value]
    if letters and all(c in _AXIS_BITS for c in letters):
        mask = 0
        for c in letters:
            mask |= _AXIS_BITS[c]
        return mask
    raise ValueError("%s: symmetry is 'auto' (the default: a part that is symmetric about a plane through the middle of its box is meshed as the mirror image of a half, so that its mesh is symmetric), None (the part is "
                     "meshed whole), or the axes to look at: 'x', 'y', 'z' or several ('xz'); not %r" % (what, value))


def symmetry_letters(mask):
    ''' The argument that stands for a bit set again (what a mesher that calls another one hands to it): None for no plane, else the letters of the planes '''
    letters = ''.join(c for c in 'xyz' if mask & _AXIS_BITS[c])
    return letters or None


def check_symmetry_with_mesh(given, mask, what):
    ''' A mesh that is given is the elements: whether it is symmetric is the person's, not the analysis' '''
    if given is not None and mask != SYMMETRY_ALL:
        raise _error('%s: symmetry has no meaning with a mesh -- the mesh is the elements (make a symmetric mesh with mesh_tet4(body, symmetry=...))' % what)


def symmetry_key(mask):
    ''' What the choice adds to the key of a problem: nothing for the default (the keys, and the results kept under them, are as they were) '''
    return {} if mask == SYMMETRY_ALL else {'symmetry': mask}


def current_symmetry():
    ''' The planes that stand now (they go into the key of a result: the same part meshed whole is another mesh) '''
    return _symmetry_stack[-1]


def _set_symmetry(mask):
    from fieldes.ffi import lib
    fn = getattr(lib, 'libfive_tetmesh_set_symmetry', None)
    if fn is None:
        if mask != SYMMETRY_ALL:
            raise _error('this FielDes library is too old for symmetry')
        return
    fn(int(mask))


class surface_fit_scope(object):
    ''' with surface_fit_scope(0.8, symmetry=0): the meshes made inside are made with the limits of that number and with the symmetry asked (a bit set; the one that stands when it is left out); what stood before stands
        again after (a scope inside a scope too) '''

    def __init__(self, fit, symmetry=None):
        self.fit = fit
        self.symmetry = symmetry            # (None: the symmetry that stands when the scope is entered -- an analysis inside a scope of another keeps its choice; a bit set otherwise, 0 for none)

    def __enter__(self):
        if self.symmetry is None:
            self.symmetry = _symmetry_stack[-1]
        _surface_fit_stack.append(self.fit)
        _symmetry_stack.append(self.symmetry)
        try:
            _set_limits(self.fit)
            _set_symmetry(self.symmetry)
        except BaseException:
            _surface_fit_stack.pop()
            _symmetry_stack.pop()
            raise
        return self

    def __exit__(self, *exc):
        _surface_fit_stack.pop()
        _symmetry_stack.pop()
        _set_limits(_surface_fit_stack[-1])
        _set_symmetry(_symmetry_stack[-1])
        return False
