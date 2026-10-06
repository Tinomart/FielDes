# Fields in the analyses: a CONSTANT field must give what the number gives, a varying one must do what it says
import sys

from fieldes import *

failures = []


def check(label, ok, detail=''):
    print(('ok    ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail != '' else ''))
    if not ok:
        failures.append(label)


def close(a, b, rel=1e-4):
    return abs(a - b) <= rel * max(abs(a), abs(b), 1e-12)


def constant(value):
    return x_field() * 0 + value


beam = box_exact((0, 0, 0), (60, 10, 10))
fixed_end = box_exact((-1, -1, -1), (0.01, 11, 11))
load_end = box_exact((59.99, -1, -1), (61, 11, 11))
conditions = static_boundary_conditions(beam, supports=[fixed(fixed_end)], loads=[force(load_end, (0, 0, -200))])
H = 2.5

alu = aluminium
base = static_analysis(beam, conditions, material=alu, element_size=H)
tip = lambda r: evaluate(r.uz, (60, 5, 5))
d0 = tip(base)
print('deflection with a number: %.5g mm, max von Mises %.4g' % (d0, base.max_von_mises))

# -- Young's modulus
as_field = Material('alu field', constant(alu.E), alu.nu, alu.density, alu.yield_strength)
r1 = static_analysis(beam, conditions, material=as_field, element_size=H)
check('E as a constant field: the same deflection', close(tip(r1), d0, 1e-3), '%.5g vs %.5g' % (tip(r1), d0))
check('E as a constant field: the same stress', close(r1.max_von_mises, base.max_von_mises, 1e-3),
      '%.5g vs %.5g' % (r1.max_von_mises, base.max_von_mises))
check('the result remembers the field material', isinstance(r1.material.E, Shape))

soft_end = Material('graded', ramp(x_field(), (0, 60), (alu.E, alu.E / 10)), alu.nu, alu.density)
r2 = static_analysis(beam, conditions, material=soft_end, element_size=H)
# (beam theory for E falling linearly to a tenth along the beam: the tip deflection is F / I times the integral of
# (L - x)^2 / E(x), against the same integral for a constant E)
def _integral(f, n=4000):
    return sum(f((i + 0.5) / n) for i in range(n)) / n


expected = _integral(lambda u: (1 - u) ** 2 / (1 - 0.9 * u)) / _integral(lambda u: (1 - u) ** 2)
check('E falling to a tenth towards the load: the deflection beam theory says (x%.3f)' % expected,
      close(abs(tip(r2)) / abs(d0), expected, 0.04), 'x%.3f' % (abs(tip(r2)) / abs(d0)))
stiff = Material('twice', constant(2 * alu.E), alu.nu, alu.density)
r3 = static_analysis(beam, conditions, material=stiff, element_size=H)
check('E twice as much: half the deflection, the same stress', close(tip(r3), d0 / 2, 1e-3) and
      close(r3.max_von_mises, base.max_von_mises, 1e-3), '%.5g (%.5g), vm %.5g' % (tip(r3), d0 / 2, r3.max_von_mises))

try:
    static_analysis(beam, conditions, material=Material('bad', constant(-5.0), 0.3), element_size=H)
    check('a negative E is refused', False)
except FeaError as e:
    check('a negative E is refused, with where', 'Young' in str(e), str(e)[:100])
try:
    Material('bad', 70e3, ramp(x_field(), (0, 1), (0.2, 0.3)))
    check('nu as a field is refused', False)
except TypeError as e:
    check('nu as a field is refused', 'nu' in str(e))
try:
    static_analysis(beam, conditions, material=as_field, element_size=H, element='hex')
    check('a field with the voxel elements is refused', False)
except FeaError as e:
    check('a field with the voxel elements is refused', "'tet'" in str(e), str(e)[:90])

# -- the profile of a load
flat = static_boundary_conditions(beam, supports=[fixed(fixed_end)],
                                  loads=[force(load_end, (0, 0, -200), profile=constant(3.0))])
r4 = static_analysis(beam, flat, material=alu, element_size=H)
check('a constant profile: the same as none', close(tip(r4), d0, 1e-3), '%.5g vs %.5g' % (tip(r4), d0))
skew = static_boundary_conditions(beam, supports=[fixed(fixed_end)],
                                  loads=[force(load_end, (0, 0, -200), profile=ramp(y_field(), (0, 10), (0.01, 1.0)))])
r5 = static_analysis(beam, skew, material=alu, element_size=H)
check('the total of a profiled load is the same', close(r5.total_load[2], -200, 1e-6), r5.total_load)
check('a profile that loads one side: the beam twists', abs(evaluate(r5.uz, (60, 10, 5)) - evaluate(r5.uz, (60, 0, 5))) > 1e-3 * abs(d0),
      '%.5g' % (evaluate(r5.uz, (60, 10, 5)) - evaluate(r5.uz, (60, 0, 5))))
try:
    force(load_end, (0, 0, ramp(x_field(), (0, 1), (0, 100))))
    check('a force component as a field is refused', False)
except TypeError as e:
    check('a force component as a field is refused, with the way', 'profile' in str(e))

# -- topology optimisation: the field is the material the design is made of
ta = topology_optimization(beam, conditions, material=alu, volume_fraction=0.5, iterations=8, element_size=5.0)
tb = topology_optimization(beam, conditions, material=as_field, volume_fraction=0.5, iterations=8, element_size=5.0)
check('topology optimisation with E as a constant field: the same compliance', close(tb.compliance[-1], ta.compliance[-1], 1e-3),
      '%.5g vs %.5g' % (tb.compliance[-1], ta.compliance[-1]))

# -- density: gravity and modal
gravity_cond = static_boundary_conditions(beam, supports=[fixed(fixed_end)], loads=[gravity()])
g0 = static_analysis(beam, gravity_cond, material=alu, element_size=H)
rho = Material('alu rho field', alu.E, alu.nu, constant(alu.density))
g1 = static_analysis(beam, gravity_cond, material=rho, element_size=H)
check('density as a constant field: the same sag under gravity', close(tip(g1), tip(g0), 1e-3), '%.5g vs %.5g' % (tip(g1), tip(g0)))
heavy_tip = Material('heavy tip', alu.E, alu.nu, ramp(x_field(), (0, 60), (alu.density, 4 * alu.density)))
g2 = static_analysis(beam, gravity_cond, material=heavy_tip, element_size=H)
check('denser towards the tip: more sag', abs(tip(g2)) > 1.5 * abs(tip(g0)), '%.5g vs %.5g' % (tip(g2), tip(g0)))

m0 = modal_analysis(beam, conditions, material=alu, modes=3, element_size=H)
m1 = modal_analysis(beam, conditions, material=rho, modes=3, element_size=H)
check('density as a constant field: the same frequencies', all(close(a, b, 1e-3) for a, b in zip(m0.frequencies, m1.frequencies)),
      ['%.4g' % f for f in m1.frequencies])
m2 = modal_analysis(beam, conditions, material=as_field, modes=3, element_size=H)
check('E as a constant field: the same frequencies', all(close(a, b, 1e-3) for a, b in zip(m0.frequencies, m2.frequencies)))
m3 = modal_analysis(beam, conditions, material=Material('light', alu.E, alu.nu, constant(alu.density / 4)), modes=3, element_size=H)
check('a quarter of the density: twice the frequency', close(m3.frequencies[0], 2 * m0.frequencies[0], 1e-3),
      '%.4g vs %.4g' % (m3.frequencies[0], 2 * m0.frequencies[0]))

# -- thermal expansion with a field
hot = static_boundary_conditions(beam, supports=[fixed(fixed_end)], loads=[thermal_expansion(constant(120.0), 20.0)])
t0 = static_analysis(beam, hot, material=alu, element_size=H)
exp_field = Material('alu exp field', alu.E, alu.nu, alu.density, alu.yield_strength, alu.conductivity, constant(alu.expansion))
t1 = static_analysis(beam, hot, material=exp_field, element_size=H)
check('expansion as a constant field: the same stress', close(t1.max_von_mises, t0.max_von_mises, 1e-3),
      '%.5g vs %.5g' % (t1.max_von_mises, t0.max_von_mises))

# -- heat
end_cold = box_exact((-1, -1, -1), (0.01, 11, 11))
end_hot = box_exact((59.99, -1, -1), (61, 11, 11))
heat = [fixed_temperature(end_cold, 20.0), heat_input(end_hot, 5.0)]
h0 = thermal_analysis(beam, heat, material=alu, element_size=H)
k_field = Material('alu k field', alu.E, alu.nu, alu.density, alu.yield_strength, constant(alu.conductivity), alu.expansion)
h1 = thermal_analysis(beam, heat, material=k_field, element_size=H)
check('conductivity as a constant field: the same temperatures', close(h1.max_temperature, h0.max_temperature, 1e-4),
      '%.6g vs %.6g' % (h1.max_temperature, h0.max_temperature))
poor_end = Material('poor end', alu.E, alu.nu, alu.density, alu.yield_strength,
                    ramp(x_field(), (0, 60), (alu.conductivity, alu.conductivity / 10)), alu.expansion)
h2 = thermal_analysis(beam, heat, material=poor_end, element_size=H)
expected_t = _integral(lambda u: 1 / (1 - 0.9 * u))        # (the rise is q times the integral of 1 / k)
check('a conductivity falling to a tenth: the temperature rise Fourier says (x%.3f)' % expected_t,
      close((h2.max_temperature - 20) / (h0.max_temperature - 20), expected_t, 0.03),
      'x%.3f' % ((h2.max_temperature - 20) / (h0.max_temperature - 20)))
fixed_field = [fixed_temperature(end_cold, constant(20.0)), heat_input(end_hot, 5.0)]
h3 = thermal_analysis(beam, fixed_field, material=alu, element_size=H)
check('a fixed temperature as a constant field: the same', close(h3.max_temperature, h0.max_temperature, 1e-4))
hot_ramp = [fixed_temperature(end_cold, ramp(y_field(), (0, 10), (20.0, 60.0))), heat_input(end_hot, 0.001)]
h4 = thermal_analysis(beam, hot_ramp, material=alu, element_size=H)
check('a fixed temperature that varies across the end: both ends of the range are there',
      evaluate(h4.temperature, (0.005, 0.5, 5)) < 30 and evaluate(h4.temperature, (0.005, 9.5, 5)) > 50,
      '%.4g .. %.4g' % (evaluate(h4.temperature, (0.005, 0.5, 5)), evaluate(h4.temperature, (0.005, 9.5, 5))))
side = box_exact((-1, -1, 9.99), (61, 11, 11))
conv = [fixed_temperature(end_cold, 20.0), heat_input(end_hot, 1.0), convection(side, 25e-6, 20.0)]
c0 = thermal_analysis(beam, conv, material=alu, element_size=H)
conv_f = [fixed_temperature(end_cold, 20.0), heat_input(end_hot, 1.0), convection(side, constant(25e-6), constant(20.0))]
c1 = thermal_analysis(beam, conv_f, material=alu, element_size=H)
check('convection with constant fields: the same', close(c1.max_temperature, c0.max_temperature, 1e-4),
      '%.6g vs %.6g' % (c1.max_temperature, c0.max_temperature))
check('...and the same balance', close(c1.heat_out, c0.heat_out, 1e-4))
prof = [fixed_temperature(end_cold, 20.0), heat_input(end_hot, 1.0, profile=ramp(y_field(), (0, 10), (0.01, 1.0)))]
p0 = thermal_analysis(beam, prof, material=alu, element_size=H)
check('a heat profile keeps the total', close(p0.heat_in, 1.0, 1e-6), p0.heat_in)
check('a heat profile heats the side with more of it', evaluate(p0.temperature, (60, 9, 5)) > evaluate(p0.temperature, (60, 1, 5)),
      '%.5g %.5g' % (evaluate(p0.temperature, (60, 9, 5)), evaluate(p0.temperature, (60, 1, 5))))
try:
    thermal_analysis(beam, heat, material=k_field, element_size=H, element='hex')
    check('a conductivity field with the voxel elements is refused', False)
except FeaError as e:
    check('a conductivity field with the voxel elements is refused', 'tet' in str(e))

# -- the flow solver takes numbers: a field is refused, with the reason
for label, make in (('an inlet speed', lambda: inlet(end_hot, speed=constant(5.0))),
                    ('an outlet pressure', lambda: outlet(end_hot, pressure=constant(1.0))),
                    ('a fluid viscosity', lambda: Fluid('x', 1e-9, constant(1e-9)))):
    try:
        make()
        check(label + ' as a field is refused', False)
    except TypeError as e:
        check(label + ' as a field is refused, with the reason', 'flow solver' in str(e))

print('FAILED: ' + ', '.join(failures) if failures else 'ALL OK')
sys.exit(1 if failures else 0)
