from fieldes import *
part = box_exact((0, 0, 0), (24, 12, 4))
bc = static_boundary_conditions(part, supports=[fixed(box_exact((-1, -1, -1), (1, 13, 5)))],
                                loads=[force(box_exact((23, -1, -1), (25, 13, 5)), (0, -50, 0))])
res = static_analysis(part, bc, material=aluminium, element_size=2.0)
d = res._display()
st = d._color_steps[4]      # load 25 %
pts = [(12, 6, 2), (23, 6, 2), (20, 11, 3)]
for p in pts:
    print('u_y full %.5g  step %.5g  (ratio %.3f)  von Mises full %.4g step %.4g' % (
        res.uy(*p), st['deform'][1](*p), st['deform'][1](*p) / res.uy(*p) if res.uy(*p) else 0,
        res.von_mises(*p), st['channels'][0][2](*p)))
print('deform scale', d._deform_scale, 'auto', d._deform_auto)
