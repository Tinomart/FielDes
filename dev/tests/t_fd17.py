# Example 17's optimised body: where is it (inside the slab only?), and the flow around it
import os
exec(open(r'C:\Users\tinmi\OneDrive\FielDes\FielDes\examples\17_flow_topology_optimization.py', encoding='utf-8').read().split('view.set_bounds')[0] + '\n' + 'W, H, T = 80.0, 40.0, 4.0\nD = 10.0\nU = 4.0\n')
post = cylinder_z(D / 2, T + 2, (24.0, 20.0, -1.0))
slab = box_exact((0, 0, 0), (W, H, T))
region = box_exact((12, 8, -1), (56, 32, T + 1))
faces = union(box_exact((-1, -1, -1), (W + 1, H + 1, 0.01)), box_exact((-1, -1, T - 0.01), (W + 1, H + 1, T + 1)))
sides = union(box_exact((-1, -1, -1), (W + 1, 0.01, T + 1)), box_exact((-1, H - 0.01, -1), (W + 1, H + 1, T + 1)))
inlet_face = box_exact((-1, -1, -1), (0.01, H + 1, T + 1))
outlet_face = box_exact((W - 0.01, -1, -1), (W + 1, H + 1, T + 1))
conditions = [slip(faces), slip(sides), inlet(inlet_face, speed=U), outlet(outlet_face, pressure=0)]
design = flow_topology_optimization(post, slab, conditions, fluid=water, objective='drag', volume=1.0, region=region,
                                    element_size=1.5, iterations=int(os.environ.get("IT", "24")), extrude="z", cache=os.environ.get("IT") is None)
print(design)
body = design.shape()
print('body bounds attr:', getattr(body, '_bounds', None))
pts = [(24, 20, 2), (24, 20, 5), (24, 20, -1), (24, 20, 4.4), (24, 20, -0.4), (60, 20, 2), (24, 20, 3.9)]
print('body at', pts, '->', ['%.2f' % v for v in evaluate(body, pts)])
xs = [12 + 0.5 * k for k in range(89)]
inside = [x for x, v in zip(xs, evaluate(body, [(x, 20, 2) for x in xs])) if v < 0]
print('body along y = 20: x %.1f .. %.1f' % (min(inside), max(inside)) if inside else 'no body on the centreline')
for x in (18, 20, 22, 24, 26, 28, 30, 32):
    ys = [8 + 0.5 * k for k in range(49)]
    ins = [y for y, v in zip(ys, evaluate(body, [(x, y, 2) for y in ys])) if v < 0]
    print('  x = %2d: y %s' % (x, ('%.1f .. %.1f (%.1f wide)' % (min(ins), max(ins), max(ins) - min(ins))) if ins else '-'))
print('volume %.1f (post %.1f)' % (design.volume, 3.14159 * 25 * T))
print('drag per iteration:', ' '.join('%.4g' % v for v in design.drag))
print('lift per iteration:', ' '.join('%.3g' % v for v in design.lift))
for k in (0, 4, 8, 12, 16, 20, len(design.levels) - 1):
    if k >= len(design.levels): break
    b = design.shape(iteration=k)
    xs = [10 + 0.5 * i for i in range(100)]
    ins = [x for x, v in zip(xs, evaluate(b, [(x, 20, 2) for x in xs])) if v < 0]
    ys = [8 + 0.5 * i for i in range(49)]
    wid = [y for y, v in zip(ys, evaluate(b, [(24, y, 2) for y in ys])) if v < 0]
    print('  iteration %2d: x %s, width at x=24 %s' % (k, ('%.1f..%.1f' % (min(ins), max(ins))) if ins else '-', ('%.1f' % (max(wid) - min(wid))) if wid else '-'))
