from fieldes import *
duct = box_exact((0, 0, 0), (20, 6, 6))
cond = [inlet(box_exact((-1, -1, -1), (0.5, 7, 7)), speed=5.0), outlet(box_exact((19.5, -1, -1), (21, 7, 7)))]
r = fluid_analysis(duct, cond, fluid=water, element_size=1.0)
print(r)
print('steps:', [s.label for s in r.steps])
for s in r.steps:
    print('  %-14s speed(10,3,3)=%.4g  pressure(1,3,3)=%.4g  max %.4g  residual %.2e' % (s.label, s.speed(10, 3, 3), s.pressure(1, 3, 3), s.max_speed, s.residual))
d = r._display()
print('display steps', len(d._color_steps), [len(st['lines']) for st in d._color_steps])
