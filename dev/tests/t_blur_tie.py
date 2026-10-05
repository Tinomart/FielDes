# The smoothed box, drawn: time, with the jitter of the copies in smooth() (it ties no longer with the sample grid)
import time
from fieldes import *
from fieldes.stdlib import fields
from fieldes.ffi import libfive_region_t, libfive_interval_t, lib

def render(shape, lo, hi, res):
    region = libfive_region_t(*[libfive_interval_t(a, b) for a, b in zip(lo, hi)])
    t = time.time()
    mesh_p = fields.render_mesh(shape, region, res)
    dt = time.time() - t
    n = mesh_p[0].tri_count
    lib.libfive_mesh_delete(mesh_p)
    return dt, n

box = box_exact((0, 0, 0), (20, 20, 20))
ex = exclude(box, sphere(6, (20, 10, 10)))
for name, s in (('box              ', box), ('box with a region', ex)):
    for steps in (1, 3):
        dt, n = render(smooth(s, 1.0, steps), (-3, -3, -3), (27, 23, 23), 5.0)
        print('%s steps %d: %7.2f s  %7d triangles  %6.1f us/triangle' % (name, steps, dt, n, 1e6 * dt / max(n, 1)))
# where the surface is: a flat face stays, to well under a micron
print('face at x=20: %.6f, x=0: %.6f, middle of the top: %.6f' % tuple(evaluate(smooth(box, 1.0, 3), [(20, 10, 10), (0, 10, 10), (10, 10, 20)])))
