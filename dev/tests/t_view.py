# headless check of the display hooks: steady + transient flow with streamlines, topology iterations, FEA / modal steps
import time
from fieldes import *

duct = box_exact((0, 0, 0), (20, 6, 6))
cond = [inlet(box_exact((-1, -1, -1), (0.5, 7, 7)), speed=5.0), outlet(box_exact((19.5, -1, -1), (21, 7, 7)))]
t0 = time.time()
r = fluid_analysis(duct, cond, fluid=water, element_size=1.0)
print(r)
d = r._display()
lines = d._flow_lines
print('steady: %d lines, points %s, range %s, %.1f s' % (len(lines), [len(l) for l in lines[:6]], d._flow_range, time.time() - t0))
if lines:
    l0 = lines[0]
    print('  line 0 first %s last %s' % (tuple(round(v, 3) for v in l0[0]), tuple(round(v, 3) for v in l0[-1])))
    print('  times monotone:', all(l0[i][4] < l0[i + 1][4] for i in range(len(l0) - 1)))
assert not hasattr(r, 'show')

t0 = time.time()
rt = fluid_analysis(duct, cond, fluid=water, element_size=1.0, time=(0.06, 0.01), store_every=2)
print(rt)
print('steps', len(rt.steps), rt.times, '%.1f s' % (time.time() - t0))
for s in rt.steps:
    print('  ', s)
dt = rt._display()
print('transient display: steps %d, step %d, lines per step %s' % (len(dt._color_steps), dt._color_step,
                                                                   [len(s['lines']) for s in dt._color_steps]))
print('  labels', [s['label'] for s in dt._color_steps])

# topology iterations
part = box_exact((0, 0, 0), (24, 12, 4))
bc = static_boundary_conditions(part, supports=[fixed(box_exact((-1, -1, -1), (1, 13, 5)))],
                                loads=[force(box_exact((23, -1, -1), (25, 13, 5)), (0, -50, 0))])
t0 = time.time()
opt = topology_optimization(part, bc, material=aluminium, volume_fraction=0.4, element_size=1.5, iterations=8)
print(opt, '%.1f s' % (time.time() - t0))
print('densities', len(opt.densities), 'compliance', len(opt.compliance))
do = opt._display()
print('topology display: steps %d, step %d, shapes %s' % (len(do._color_steps), do._color_step,
                                                          [('shape' in s) for s in do._color_steps]))
print('  density at the middle, iterations 1 and last: %.3f %.3f' % (opt.densities[0].value(12, 6, 2) if hasattr(opt.densities[0], 'value') else -1,
                                                                    opt.densities[-1].value(12, 6, 2) if hasattr(opt.densities[-1], 'value') else -1))

# static + modal
t0 = time.time()
res = static_analysis(part, bc, material=aluminium, element_size=2.0)
ds = res._display()
print('static display: steps %d (%s .. %s), step %d, %.1f s' % (len(ds._color_steps), ds._color_steps[0]['label'],
                                                               ds._color_steps[-1]['label'], ds._color_step, time.time() - t0))
assert 'channels' in ds._color_steps[0] and 'channels' not in ds._color_steps[-1]
modes = modal_analysis(part, bc, material=aluminium, modes=2, element_size=2.0)
dm = modes._display()
print('modal display: steps %d, step %d, labels %s' % (len(dm._color_steps), dm._color_step, [s['label'] for s in dm._color_steps[:4]]))
th = thermal_analysis(part, [heat_input(box_exact((23, -1, -1), (25, 13, 5)), 2.0), fixed_temperature(box_exact((-1, -1, -1), (1, 13, 5)), 20)],
                      material=aluminium, element_size=2.0)
print('thermal display fields', len(th._display()._color_fields))
print('ALL-OK')
