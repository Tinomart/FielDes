# where the time of a part's exact field goes: the tessellation (kernel, once per solid) or the field's structure
import os, time, ctypes
from fieldes import *
from fieldes.ffi import lib, libfive_mesh_import_info_t
from fieldes.stdlib import cad_import

here = os.path.dirname(os.path.abspath(__file__))
path = os.path.normpath(os.path.join(here, '..', '..', 'examples', 'step', 'Keukencombinatie.stp'))
t = time.time(); kitchen = import_step_parts(path); print('import %.1f s' % (time.time() - t))
tess = build = 0.0
tris_total = 0
for i, (part, _) in enumerate(kitchen):
    src = cad_import._sources_of(part)[0]
    mat = (ctypes.c_double * 16)(*[float(v) for row in src.scale for v in row])
    t = time.time()
    mesh = lib.libfive_step_exact_surface(src.path.encode('utf-8'), int(src.solid), int(src.instance), mat, 64)
    t1 = time.time() - t
    mc = mesh.contents
    t = time.time()
    info = libfive_mesh_import_info_t()
    ptr = lib.libfive_mesh_from_arrays(ctypes.cast(mc.verts, ctypes.POINTER(ctypes.c_float)), mc.vert_count,
                                       ctypes.cast(mc.tris, ctypes.POINTER(ctypes.c_uint32)), mc.tri_count, 1.0, ctypes.byref(info))
    t2 = time.time() - t
    tris_total += mc.tri_count
    lib.libfive_mesh_delete(mesh)
    tess += t1; build += t2
    if i < 12 or t1 + t2 > 2:
        print('part %2d: %6d tris, tessellation %.2f s, field %.2f s' % (i, mc.tri_count, t1, t2))
print('90 parts: %d triangles, tessellation %.1f s, field structure %.1f s' % (tris_total, tess, build))
