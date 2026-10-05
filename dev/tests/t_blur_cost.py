# What drawing a smoothed shape costs by the number of copies: a sphere, a box, and the excluded box of the window test,
# smoothed by 0..3 steps (1, 6, 19, 44 copies of the field), meshed over the same region
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
cases = (('sphere r=8      ', sphere(8, (10, 10, 10))), ('box_exact 20    ', box), ('box with a region', ex))
for name, s in cases:
    for steps in (0, 1, 2, 3):
        shape = s if steps == 0 else smooth(s, 1.0, steps)
        dt, n = render(shape, (-3, -3, -3), (27, 23, 23), 5.0)
        print('%s steps %d: %7.2f s  %7d triangles  %6.1f us/triangle' % (name, steps, dt, n, 1e6 * dt / max(n, 1)))
