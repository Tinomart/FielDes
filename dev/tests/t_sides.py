# Example 16's flow: the speed along the slip sides of the slab (y = 0 and y = H) must not be zero
from fieldes import *
W, H, T = 80.0, 40.0, 4.0
D, U = 10.0, 4.0
post = cylinder_z(D / 2, T + 2, (24.0, 20.0, -1.0))
fluid_domain = difference(box_exact((0, 0, 0), (W, H, T)), post)
faces = union(box_exact((-1, -1, -1), (W + 1, H + 1, 0.01)), box_exact((-1, -1, T - 0.01), (W + 1, H + 1, T + 1)))
sides = union(box_exact((-1, -1, -1), (W + 1, 0.01, T + 1)), box_exact((-1, H - 0.01, -1), (W + 1, H + 1, T + 1)))
inlet_face = box_exact((-1, -1, -1), (0.01, H + 1, T + 1))
outlet_face = box_exact((W - 0.01, -1, -1), (W + 1, H + 1, T + 1))
flow = fluid_analysis(fluid_domain, [slip(faces), slip(sides), inlet(inlet_face, speed=U), outlet(outlet_face, pressure=0)],
                      fluid=water, element_size=1.0)
print(flow)
for x in (10.0, 40.0, 70.0):
    for z in (2.0, 4.0):
        ys = [0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 20.0, 36.0, 38.0, 39.0, 39.5, 39.75, 40.0]
        vals = evaluate(flow.speed, [(x, y, z) for y in ys])
        print('x %4.0f z %.0f: ' % (x, z) + ' '.join('y%g=%.2f' % (y, v) for y, v in zip(ys, vals)))
# the same on the top face, just inside and just outside the slab
print('top face z = 4 vs 3.9 vs 4.1 at (40, 20):', evaluate(flow.speed, [(40, 20, 4.0), (40, 20, 3.9), (40, 20, 4.1)]))
print('side y = 0 vs -0.1 at (40, *, 2):', evaluate(flow.speed, [(40, 0.0, 2.0), (40, -0.1, 2.0), (40, 0.1, 2.0)]))
