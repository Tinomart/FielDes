'''
The elements of a result made of tetrahedra, for the viewer: what FielDes draws for "Elements" on the result card and cuts through, whole, with the section view -- the mesh the solver used,
and each tetrahedron's own value of each field.  One function for every analysis (the static, the thermal, the flow and the modes of a modal analysis): the kernel gives each its mesh
and its element values, and the result hands over what this builds as `_fea_mesh`.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes

from fieldes.i18n import tr as _tr

# H198 (the user, 2026-11-13: "Make element shown by default for every analysis. Remove the threshold at which they are not shown, elements should always be shown"): there is no limit on
# the number of elements handed to the viewer any more -- every result with elements has them to show, and the Elements view is on by default.  (It was 8 million tetrahedra, and the card said
# that none were shown over it: H186.)


def elements_note(count):
    ''' What the card says of a result that has elements it does not show: nothing, ever (no result is too large to show its elements) '''
    return ''


def default_text(nodes, elements):
    ''' What the card says of the elements: their number and the number of nodes '''
    return _tr('%s tetrahedra · %s nodes') % ('{:,}'.format(elements), '{:,}'.format(nodes))


def element_mesh(counts, mesh, values, field_ids, text, displacement=None):
    ''' (vertices, tetrahedra, boundary triangles, the tetrahedron of each, text, [each field's value in every tetrahedron], the displacement at every vertex) as float32 / int32 bytes: what
        `_fea_mesh` says to the viewer.  None when there are no tetrahedra or more than are handed over.
          counts(out)                             fills the numbers of vertices, tetrahedra and boundary triangles of the solved problem
          mesh(vertices, tets, faces, face_tet)   fills the mesh
          values(field, out, capacity)            fills one field's value in each tetrahedron
          field_ids                               the kernel's number of each field, in the order the result lists them (the card's order)
          text                                    what the card says of them (None: the number of tetrahedra and nodes)
          displacement(out)                       fills the displacement at the vertices (3 floats each); None: the part does not move '''
    c = (ctypes.c_int64 * 3)()
    counts(c)
    nv, nt, nf = int(c[0]), int(c[1]), int(c[2])
    if not nt > 0:
        return None
    verts = (ctypes.c_float * (3 * nv))()
    tets = (ctypes.c_int32 * (4 * nt))()
    faces = (ctypes.c_int32 * (3 * nf))()
    face_tet = (ctypes.c_int32 * nf)()
    mesh(verts, tets, faces, face_tet)
    disp = (ctypes.c_float * (3 * nv))()
    if displacement is not None:
        displacement(disp)
    per_field = []
    for f in field_ids:
        buf = (ctypes.c_float * nt)()
        values(f, buf, nt)
        per_field.append(bytes(buf))
    return (bytes(verts), bytes(tets), bytes(faces), bytes(face_tet), text if text is not None else default_text(nv, nt), per_field, bytes(disp))
