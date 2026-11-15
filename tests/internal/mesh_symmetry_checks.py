'''
Checks of the symmetry of meshes (H226; the user, 2026-11-16: "I meshed the bracket myself from a file and the mesh is just not symmetrical, despite the part being symmetrical"; "THAT APPLIES FOR ALL MESHING CORRECT?";
"this should certainly be an argument for each meshing that can turn it off. But I would definitely have it on by default").  A part that is symmetric about a plane through the middle of its box is meshed as the mirror image of
a half, by every meshing function, and `symmetry=None` turns it off.  Every mesh is compared with ITS OWN MIRROR IMAGE: the nodes, and the cells and boundary faces by their nodes, about the middle plane of the mesh -- a mesh that is
symmetric has no node, cell or face without its image.  Run as a FielDes file in the window (the part is then in the viewport as well).
'''
import math
import sys

from fieldes import *

bad = []
SIZES = {'tet4': 4, 'tet10': 10, 'hex8': 8, 'hex20': 20, 'prism6': 6, 'pyra5': 5}
FACE_SIZES = {'tri3': 3, 'quad4': 4}


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def key_of(P, ids, q):
    ''' A cell or a face as the sorted tuple of its nodes' places, in units of q (so that two meshes of the same places have the same keys) '''
    return tuple(sorted((round(P[3 * n] / q), round(P[3 * n + 1] / q), round(P[3 * n + 2] / q)) for n in ids))


def mirrored_key(P, ids, axis, at, q):
    out = []
    for n in ids:
        p = [P[3 * n], P[3 * n + 1], P[3 * n + 2]]
        p[axis] = 2.0 * at - p[axis]
        out.append((round(p[0] / q), round(p[1] / q), round(p[2] / q)))
    return tuple(sorted(out))


def unmatched(mesh, axis, h):
    ''' (the nodes, the cells, the faces) of the mesh that have no mirror image about the middle plane of its nodes along `axis`: counts, and the totals '''
    P, n = mesh.nodes, mesh.node_count
    lo, hi = min(P[3 * i + axis] for i in range(n)), max(P[3 * i + axis] for i in range(n))
    at = 0.5 * (lo + hi)
    q = 1e-4 * h
    nodes = set((round(P[3 * i] / q), round(P[3 * i + 1] / q), round(P[3 * i + 2] / q)) for i in range(n))
    miss_nodes = 0
    for i in range(n):
        p = [P[3 * i], P[3 * i + 1], P[3 * i + 2]]
        p[axis] = 2.0 * at - p[axis]
        if (round(p[0] / q), round(p[1] / q), round(p[2] / q)) not in nodes:
            miss_nodes += 1
    miss_cells, cells = 0, 0
    for kind, arr in mesh.cells.items():
        size = SIZES.get(kind)
        if not size:
            continue
        have = set(key_of(P, arr[size * c:size * c + size], q) for c in range(len(arr) // size))
        for c in range(len(arr) // size):
            cells += 1
            if mirrored_key(P, arr[size * c:size * c + size], axis, at, q) not in have:
                miss_cells += 1
    miss_faces, faces = 0, 0
    for kind, arr in mesh.faces.items():
        size = FACE_SIZES.get(kind)
        if not size:
            continue
        have = set(key_of(P, arr[size * c:size * c + size], q) for c in range(len(arr) // size))
        for c in range(len(arr) // size):
            faces += 1
            if mirrored_key(P, arr[size * c:size * c + size], axis, at, q) not in have:
                miss_faces += 1
    return (miss_nodes, n), (miss_cells, cells), (miss_faces, faces)


def symmetric_about(label, mesh, axes, h):
    for axis in axes:
        nodes, cells, faces = unmatched(mesh, axis, h)
        ok = nodes[0] == 0 and cells[0] == 0 and faces[0] == 0
        check('%s: symmetric about the middle plane of the %s axis (no node, cell or face without its image)' % (label, 'xyz'[axis]), ok,
              'nodes %d of %d, cells %d of %d, faces %d of %d without one' % (nodes[0], nodes[1], cells[0], cells[1], faces[0], faces[1]))


def main():
    h = 2.0
    plate = difference(box((40, 20, 6), position=(0, 0, 0)), cylinder(4, 10, position=(0, 0, 0)))                 # symmetric about x, y and z
    off = difference(box((40, 20, 6), position=(0, 0, 0)), cylinder(4, 10, position=(8, 0, 0)))                   # symmetric about y and z only
    ell = union(box((30, 10, 6), position=(15, 5, 3)), box((10, 30, 6), position=(5, 15, 3)))                     # an L: symmetric about a diagonal plane, none through the middle of its box

    # --- the tetrahedra: on by default, off with None, restricted with the letters
    m = mesh_tet4(plate, h)
    check('mesh_tet4 of a plate that is symmetric about x, y and z is mirrored about planes by default (%s)' % getattr(m, 'mirrored', '?'), getattr(m, 'mirrored', 0) >= 2)
    symmetric_about('mesh_tet4 of the plate', m, (0, 1), h)
    w = mesh_tet4(plate, h, symmetry=None)
    check('mesh_tet4(symmetry=None) is the whole plate, not mirrored', getattr(w, 'mirrored', 1) == 0)
    one = mesh_tet4(plate, h, symmetry='y')
    check('mesh_tet4(symmetry=\'y\') is mirrored about one plane (%s)' % getattr(one, 'mirrored', '?'), getattr(one, 'mirrored', 0) == 1)
    symmetric_about('mesh_tet4(symmetry=\'y\')', one, (1,), h)
    o = mesh_tet4(off, h)
    check('a plate whose hole is off the middle in x is symmetric about y only: mirrored about 1 plane (%s)' % getattr(o, 'mirrored', '?'), getattr(o, 'mirrored', 0) == 1)
    symmetric_about('mesh_tet4 of the plate with the hole off the middle', o, (1,), h)
    l = mesh_tet4(ell, h)
    check('an L has no plane through the middle of its box: not mirrored', getattr(l, 'mirrored', 1) == 0)
    t10 = mesh_tet10(plate, 2.5)
    check('mesh_tet10 is mirrored too (%s)' % getattr(t10, 'mirrored', '?'), getattr(t10, 'mirrored', 0) >= 2)
    symmetric_about('mesh_tet10 of the plate', t10, (0, 1), 2.5)
    # (the middle nodes of the edges on a curved surface are put on it by Newton steps on a field read in single precision: a node and its image came out 0.03 mm apart on the real bracket, a few 0.3 mm; they are put on it on one
    # side of the planes and mirrored now)
    ball10 = mesh_tet10(sphere(8), 3.0)
    check('mesh_tet10 of a ball is mirrored about three planes (%s)' % getattr(ball10, 'mirrored', '?'), getattr(ball10, 'mirrored', 0) == 3)
    symmetric_about('mesh_tet10 of the ball (middle nodes on the curved surface)', ball10, (0, 1, 2), 3.0)
    try:
        mesh_tet4(plate, h, symmetry='w')
        check('a symmetry that is no axis is refused', False)
    except ValueError:
        check('a symmetry that is no axis is refused', True)

    # --- the grids of cubes and the cut cells
    for name, make in (('mesh_hex_quanta', mesh_hex_quanta), ('mesh_hex20_quanta', mesh_hex20_quanta), ('mesh_cartesian', mesh_cartesian)):
        g = make(plate, h)
        symmetric_about(name + ' of the plate', g, (0, 1), h)
        gw = make(plate, h, symmetry=None)
        check('%s(symmetry=None) is the whole plate: no note of a mirror' % name, not any('mirror' in note for note in gw.notes))
        check('%s says it was mirrored' % name, any('mirror' in note for note in g.notes), g.notes)

    # --- the hexahedra of an extrusion
    e = mesh_hex_extrusion(plate, h)
    symmetric_about('mesh_hex_extrusion of the plate', e, (0, 1), h)
    ew = mesh_hex_extrusion(plate, h, symmetry=None)
    check('mesh_hex_extrusion(symmetry=None) makes the end face whole', not any('mirror' in note for note in ew.notes))

    # --- the surface meshes
    s = mesh_surface_tri(plate, h)
    symmetric_about('mesh_surface_tri of the plate', s, (0, 1), h)
    sq = mesh_surface_quad(plate, h)
    symmetric_about('mesh_surface_quad of the plate', sq, (0, 1), h)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        bad.append('an error')
    print('ALL CHECKS PASSED' if not bad else 'FAILED: ' + '; '.join(bad))
    sys.exit(1 if bad else 0)
