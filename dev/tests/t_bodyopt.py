# The post in the slab, optimised for the least drag with its volume kept (2D through the slab): expect a streamlined body
import os, time
from fieldes import *
W, H, T = 60.0, 30.0, 3.0
D, U = 8.0, 4.0
h = float(os.environ.get('H1', '1.5'))
post = cylinder_z(D / 2, T + 2, (20.0, 15.0, -1.0))
slab = box_exact((0, 0, 0), (W, H, T))
faces = union(box_exact((-1, -1, -1), (W + 1, H + 1, 0.01)), box_exact((-1, -1, T - 0.01), (W + 1, H + 1, T + 1)))
sides = union(box_exact((-1, -1, -1), (W + 1, 0.01, T + 1)), box_exact((-1, H - 0.01, -1), (W + 1, H + 1, T + 1)))
cond = [slip(faces), slip(sides), inlet(box_exact((-1, -1, -1), (0.01, H + 1, T + 1)), speed=U),
        outlet(box_exact((W - 0.01, -1, -1), (W + 1, H + 1, T + 1)))]
region = box_exact((8, 5, -1), (44, 25, T + 1))
t0 = time.time()
opt = flow_topology_optimization(post, slab, cond, fluid=water, objective='drag', volume=1.0, region=region,
                              element_size=h, iterations=int(os.environ.get('IT', '24')), extrude='z', cache=False)
print(opt, '%.0f s' % (time.time() - t0))
print('drag per iteration:', ' '.join('%.3g' % v for v in opt.drag))
print('lift per iteration:', ' '.join('%.3g' % v for v in opt.lift))
print('directions', opt.flow_direction, opt.lift_direction, 'volume %.4g (post %.4g)' % (opt.volume, 3.14159 * 16 * T))
body = opt.shape()
xs = [8 + 0.5 * k for k in range(73)]
inside = [x for x, v in zip(xs, evaluate(body, [(x, 15, T / 2) for x in xs])) if v < 0]
print('body along the flow at y = 15: x from %.1f to %.1f (%.1f long)' % (min(inside), max(inside), max(inside) - min(inside)) if inside else 'no body on the centreline')
for x in (16, 18, 20, 22, 24, 26, 28):
    ys = [5 + 0.5 * k for k in range(41)]
    ins = [y for y, v in zip(ys, evaluate(body, [(x, y, T / 2) for y in ys])) if v < 0]
    print('  x = %2d: y %s' % (x, ('%.1f .. %.1f (%.1f wide)' % (min(ins), max(ins), max(ins) - min(ins))) if ins else '-'))
d = opt._display()
print('display: %d shapes, fluid steps %d, body steps %d' % (len(d), len(getattr(d[0], '_color_steps', [])), len(getattr(d[1], '_color_steps', []))))
print('flow around the final body:', opt.flow)
