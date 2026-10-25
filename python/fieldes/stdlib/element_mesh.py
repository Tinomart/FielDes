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

# More tetrahedra than this are not handed to the viewer (every field's value in every one of them goes with them: about 100 bytes a tetrahedron, and the viewer's copies of them): the
# card says so instead of showing no button.  The default mesh of a solid part is already millions of them (the largest extent over 60: a cube has 2.7 million), and the elements are
# what a user opens a result to see
MAX_DRAWN = 8000000


def elements_note(count):
    ''' What the card says of a result with more tetrahedra than are drawn: how many it has, and that none are shown ('' when it is not too many) '''
    if count <= MAX_DRAWN:
        return ''
    return _tr('%s tetrahedra: more than the %s that can be drawn, so the elements are not shown') % ('{:,}'.format(count), '{:,}'.format(MAX_DRAWN))


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
    if not 0 < nt <= MAX_DRAWN:
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
