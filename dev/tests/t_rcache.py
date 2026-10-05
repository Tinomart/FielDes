# The result cache: every analysis solved once, read back in a later process with the same numbers, the same field
# keys, and no solve.  Run twice (RUN=1, RUN=2) with FIELDES_RESULT_CACHE_DIR pointing at one folder.
import os, sys, time, json
from fieldes import *
from fieldes.stdlib import result_cache
from fieldes.stdlib.content_cache import shape_key

run = int(os.environ.get('RUN', '1'))
out = {}

part = difference(box_exact((0, 0, 0), (40, 20, 10)), cylinder_z(3, 12, (20, 10, -1)))
part._bounds = ((0, 0, 0), (40, 20, 10))
left = box_exact((-1, -1, -1), (0.5, 21, 11))
right = box_exact((39.5, -1, -1), (41, 21, 11))
top = box_exact((15, -1, 9.5), (25, 21, 11))

t0 = time.time()
cond = static_boundary_conditions(part, supports=[fixed(left)], loads=[force(top, (0, 0, -200))])
st = static_analysis(part, cond, material=aluminium, element_size=2.0)
out['static'] = (round(st.max_von_mises, 6), round(st.max_displacement, 9), shape_key(st.von_mises)[1], st.elements)
out['static_s'] = time.time() - t0

t0 = time.time()
sh = static_analysis(part, cond, material=aluminium, element_size=3.0, element='hex')
out['hex'] = (round(sh.max_von_mises, 6), shape_key(sh.von_mises)[1], sh.elements)
out['hex_s'] = time.time() - t0

t0 = time.time()
mo = modal_analysis(part, cond, material=aluminium, modes=3, element_size=2.5)
out['modal'] = ([round(f, 4) for f in mo.frequencies], shape_key(mo.modes[0].displacement)[1])
out['modal_s'] = time.time() - t0

t0 = time.time()
th = thermal_analysis(part, [fixed_temperature(left, 20.0), heat_input(right, 5.0)], material=aluminium, element_size=2.0)
out['thermal'] = (round(th.max_temperature, 6), shape_key(th.temperature)[1], th.elements)
out['thermal_s'] = time.time() - t0

t0 = time.time()
to = topology_optimization(part, cond, material=aluminium, volume_fraction=0.4, element_size=2.5, iterations=6)
out['topology'] = ([round(c, 6) for c in to.compliance], len(to.densities), shape_key(to.density)[1],
                   shape_key(to.densities[1])[1])
out['topology_s'] = time.time() - t0

slab = box_exact((0, 0, 0), (30, 15, 3))
post = cylinder_z(2.5, 5, (10, 7.5, -1))
dom = difference(slab, post)
faces = union(box_exact((-1, -1, -1), (31, 16, 0.01)), box_exact((-1, -1, 2.99), (31, 16, 4)))
sides = union(box_exact((-1, -1, -1), (31, 0.01, 4)), box_exact((-1, 14.99, -1), (31, 16, 4)))
fc = [slip(faces), slip(sides), inlet(box_exact((-1, -1, -1), (0.01, 16, 4)), speed=2.0),
      outlet(box_exact((29.99, -1, -1), (31, 16, 4)))]
t0 = time.time()
fl = fluid_analysis(dom, fc, fluid=water, element_size=1.0)
out['flow'] = (round(fl.pressure_drop, 14), round(fl.max_speed, 6), len(fl.steps), shape_key(fl.speed)[1],
               shape_key(fl.steps[0].speed)[1], len(fl.streamlines(count=5)), [round(x, 12) for x in fl.wall_force])
out['flow_s'] = time.time() - t0

t0 = time.time()
ft = fluid_analysis(dom, fc, fluid=water, element_size=1.5, time=(0.5, 0.1))
out['flow_time'] = (len(ft.steps), [round(t, 3) for t in ft.times], round(ft.max_speed, 6), shape_key(ft.steps[2].pressure)[1])
out['flow_time_s'] = time.time() - t0

t0 = time.time()
fo = flow_topology_optimization(post, slab, fc, fluid=water, objective='drag', volume=1.0,
                                region=box_exact((4, 2, -1), (22, 13, 4)), element_size=1.5, iterations=3, extrude='z')
out['flow_topology'] = ([round(d, 12) for d in fo.drag], len(fo.levels), round(fo.volume, 6), shape_key(fo.level)[1],
                        shape_key(fo.flow.speed)[1], len(fo.flow.steps))
out['flow_topology_s'] = time.time() - t0

print('RESULT', json.dumps(out, sort_keys=True))
print('cache stats', result_cache.stats())
