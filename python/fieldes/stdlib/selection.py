'''
Selecting a surface: the flood fill of a CAD program, as a field.

    from fieldes import *

    top = select_surface(part, seed=(12.5, 40.0, -3.0), angle=10)
    top                                       # displayed: the part's surface, only the patch of it, lit up

`select_surface` picks the patch of the part's surface around `seed` (a point on or near it) by spreading over
the neighbouring triangles of the surface mesh: with mode='flat' (the default) as long as the surface stays
within `angle` degrees of the way it faced at the seed -- a flat face, or a gently curved one; with mode='smooth'
as long as it turns less than `angle` degrees from one triangle to the next -- a cylinder, a fillet, a whole
rounded skin, up to a sharp edge.  `radius` stops it that far from the seed.

What comes back is a field, like everything else here: negative in a thin layer (`thickness`) across the
patch, positive elsewhere, so it is a region you can give to `fixed()` and `force()`, show on the part, combine
with other shapes, or hand to `lattice_surface_conform()` as the surface to put a lattice on:

    conditions = static_boundary_conditions(part, [fixed(select_surface(part, (0, 0, 0)))],
                                            [force(top, (0, -100, 0))])
    result = static_analysis(part, conditions, material=aluminium)

In FielDes, right-click a surface in the viewport: the menu holds the angle, the mode and the thickness, and
writes the `select_surface(...)` line into the script under the part, like everything else the program does.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import array
import ctypes

from fieldes.ffi import lib, libfive_region_t, libfive_interval_t, libfive_mesh_import_info_t
from fieldes.shape import Shape
from fieldes.stdlib.content_cache import content_cached
from fieldes.stdlib.fields import _s, _shape_bounds, render_mesh, _vars_key

__all__ = ['select_surface', 'SurfaceSelection']

MODES = {'flat': 0, 'smooth': 1}
SELECTION_CATEGORY = 6          # (its colour in the application's colour map "bc")


class SurfaceSelection(Shape):
    ''' A patch of a surface (see select_surface): a field, negative in a thin layer across the patch.
        .shape (what it was picked on), .seed, .angle, .mode, .thickness, .triangles (how many of the surface
        mesh's), .patch (the unsigned distance to the patch itself, a field), .whole (the same to the whole
        surface mesh: it equals .patch exactly where the nearest point of the surface is in the patch),
        .arrays (the patch's own triangles) '''

    _no_handles = True             # (not something to drag: FielDes hides the handles button)

    def _display(self):
        ''' What FielDes shows for the selection stated on its own: the part it was picked on, drawn only where the
            patch is, in the selection's colour -- the vertices of the part's surface mesh that belong to the patch
            are coloured, all the others are not drawn.  (The field itself is a layer a hundredth of the part thick:
            drawn as it is, it comes out as a few disconnected fragments at the resolution of the viewport.)  A
            vertex belongs to the patch when the nearest point of the surface is in it, that is, when its distance
            to the patch equals its distance to the whole surface (to within half the layer's thickness). '''
        from fieldes.stdlib.fea import colored
        value = self.patch - self.whole - self.thickness / 2.0       # (<= 0 on the patch)
        shown = colored(self.shape, value, range=(0.0, float(SELECTION_CATEGORY)),
                        label='bc:{}'.format(SELECTION_CATEGORY), colormap='bc')
        shown._color_cutoff = 0.0              # (where the value is above this, the surface is not drawn)
        return shown

    def __repr__(self):
        return 'select_surface(seed={}, angle={:g}, mode={!r}): {} triangles, {:g} mm thick'.format(
            tuple(round(c, 3) for c in self.seed), self.angle, self.mode, self.triangles, self.thickness)


@content_cached('select_surface', limit=16)
def _patch(shape, seed, angle, mode, radius, resolution, lo, hi, script_vars):
    ''' (unsigned distance to the patch, triangles in it, distance of the seed from the surface, the patch's
        box, triangles of the surface, unsigned distance to the whole surface mesh) '''
    size = max(hi[i] - lo[i] for i in range(3))
    pad = 0.03 * size
    region = libfive_region_t(*[libfive_interval_t(a - pad, b + pad) for a, b in zip(lo, hi)])
    mesh_p = render_mesh(shape, region, resolution)
    if not mesh_p:
        raise ValueError('select_surface: the shape could not be meshed')
    try:
        mesh = mesh_p[0]
        if mesh.tri_count == 0:
            raise ValueError('select_surface: the shape has no surface in its bounds')
        verts = ctypes.cast(mesh.verts, ctypes.POINTER(ctypes.c_float))
        tris = ctypes.cast(mesh.tris, ctypes.POINTER(ctypes.c_uint32))
        selected = (ctypes.c_uint8 * mesh.tri_count)()
        seed_arr = (ctypes.c_float * 3)(*seed)
        distance = ctypes.c_float(0.0)
        count = lib.libfive_mesh_flood(verts, mesh.vert_count, tris, mesh.tri_count, seed_arr, angle,
                                       MODES[mode], radius, selected, ctypes.byref(distance))
        if count < 0:
            raise ValueError('select_surface: ' + lib.libfive_import_mesh_last_message().decode('utf-8', 'replace'))
        info = libfive_mesh_import_info_t()
        ptr = lib.libfive_mesh_patch(verts, mesh.vert_count, tris, mesh.tri_count, selected, ctypes.byref(info))
        # the same mesh as a whole: where the distance to the patch equals the distance to it, the nearest
        # point of the surface is in the patch (what a lattice that follows the patch needs, exactly)
        everything = (ctypes.c_uint8 * mesh.tri_count)()
        ctypes.memset(everything, 1, mesh.tri_count)
        info_all = libfive_mesh_import_info_t()
        ptr_all = lib.libfive_mesh_patch(verts, mesh.vert_count, tris, mesh.tri_count, everything,
                                         ctypes.byref(info_all))
        triangles = mesh.tri_count
        # the triangles of the patch themselves (a lattice that follows the face reads its surface off them)
        n_v = mesh.vert_count
        used = {}
        pv = array.array('f')
        pt = array.array('I')
        for ti in range(mesh.tri_count):
            if not selected[ti]:
                continue
            for k in range(3):
                vi = tris[3 * ti + k]
                at = used.get(vi)
                if at is None:
                    at = used[vi] = len(used)
                    pv.extend((verts[3 * vi], verts[3 * vi + 1], verts[3 * vi + 2]))
                pt.append(at)
        arrays = (pv.tobytes(), pt.tobytes())
    finally:
        lib.libfive_mesh_delete(mesh_p)
    if not ptr or not ptr_all:
        raise ValueError('select_surface: ' + lib.libfive_import_mesh_last_message().decode('utf-8', 'replace'))
    box = ((info.bounds.X.lower, info.bounds.Y.lower, info.bounds.Z.lower),
           (info.bounds.X.upper, info.bounds.Y.upper, info.bounds.Z.upper))
    return Shape(ptr), int(count), float(distance.value), box, int(triangles), Shape(ptr_all), arrays


def select_surface(shape, seed, angle=15.0, mode='flat', thickness=None, radius=None, resolution=None,
                   bounds=None):
    ''' The patch of the surface of `shape` around the point `seed`, found by a flood fill over the triangles of
        its surface (see the module), as a field: negative in a layer `thickness` mm thick across the patch.

        seed        a point on (or near) the surface: (x, y, z)
        angle       degrees: with mode='flat' how far a triangle may face from the way the seed's does, with
                    mode='smooth' how much the surface may turn from one triangle to the next
        mode        'flat' (a face, flat or gently curved) or 'smooth' (round faces, up to a sharp edge)
        thickness   mm (default: a hundredth of the shape's size, at least two cells of the surface mesh)
        radius      mm: stop this far from the seed (default: no limit)
        resolution  samples per mm of the surface mesh the fill runs over (default: about 200 along the
                    longest side, at least 1): finer follows small faces
        bounds      ((x0, y0, z0), (x1, y1, z1)) of the shape, if it cannot be found

        Returns a SurfaceSelection: a shape (so it is shown, hidden, deleted like any), usable as a region:
        fixed(selection), force(selection, ...), lattice_surface_conform(shape, surface=selection, ...). '''
    shape = _s(shape)
    if mode not in MODES:
        raise ValueError("select_surface: mode is 'flat' or 'smooth'")
    seed = tuple(float(c) for c in seed)
    if len(seed) != 3:
        raise ValueError('select_surface: seed is a point (x, y, z)')
    lo, hi = _shape_bounds(shape, bounds)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    res = float(resolution) if resolution else max(1.0, 200.0 / size)
    field, count, distance, box, triangles, whole, arrays = _patch(
        shape, seed, float(angle), mode, float(radius or 0.0), res, tuple(lo), tuple(hi), _vars_key())
    t = float(thickness) if thickness is not None else max(0.01 * size, 2.0 / res)
    if distance > 0.05 * size:
        print('select_surface: the seed is {:.3g} mm from the surface (the patch is the nearest one)'.format(distance))
    band = field - t / 2.0            # (named: a temporary would free its tree before the copy is made)
    out = SurfaceSelection(lib.libfive_tree_copy(band.ptr))
    out.shape, out.seed, out.angle, out.mode = shape, seed, float(angle), mode
    out.thickness, out.triangles, out.patch, out.whole = t, count, field, whole
    out.arrays = arrays                # (the patch's vertices and triangles, as bytes of floats and unsigned ints)
    out._bounds = (tuple(box[0][i] - t for i in range(3)), tuple(box[1][i] + t for i in range(3)))
    # shown lit up (the colour of the "selected" category of the application's boundary-condition colours)
    out._color_field = Shape.wrap(float(SELECTION_CATEGORY))
    out._color_range = (0.0, float(SELECTION_CATEGORY))
    out._color_label = 'bc:{}'.format(SELECTION_CATEGORY)
    out._color_map = 'bc'
    return out
