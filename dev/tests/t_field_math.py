# Arithmetic on fields (field * field, 2 ** field ...), field_from_body, a Point where a position goes, and what the model
# tree is told about the slots a drop can fill (a field takes the first number that is no count, a point the first position)
import ast
import json

from fieldes import *
from fieldes import runner, app_support
from fieldes.kinds import kind_of

failures = []


def check(label, ok, detail=''):
    print(('ok    ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail != '' else ''))
    if not ok:
        failures.append(label)


pts = [(1, 2, 3), (5, 5, 5), (-3, 4, 8)]
a = distance_to_point((0, 0, 0))
b = x_field() + 10
body = sphere(4, (0, 0, 0))

check('field * field is a field', kind_of(a * b) == 'field')
check('field * field has the product of the values',
      all(abs(u - v * w) < 1e-4 for u, v, w in zip(evaluate(a * b, pts), evaluate(a, pts), evaluate(b, pts))))
check('field / field, field ** field, 2 ** field, field - number',
      [kind_of(a / b), kind_of(a ** b), kind_of(2 ** b), kind_of(a - 3)] == ['field'] * 4)
check('2 ** field is 2 to the field', abs(evaluate(2 ** b, [(1, 0, 0)])[0] - 2 ** 11) < 1e-2)
check('the function forms are fields and agree with the operators',
      kind_of(multiply_fields(a, b)) == 'field' and
      max(abs(u - v) for u, v in zip(evaluate(multiply_fields(a, b), pts), evaluate(a * b, pts))) < 1e-4)
check('subtract / divide fold from the left',
      abs(evaluate(subtract_fields(a, 1, 1), [(3, 0, 0)])[0] - 1) < 1e-4 and abs(evaluate(divide_fields(b, 2, 5), [(0, 0, 0)])[0] - 1) < 1e-4)
fb = field_from_body(body)
check('field_from_body: a field with the values of the body', kind_of(fb) == 'field' and kind_of(body) == 'solid' and
      evaluate(fb, pts) == evaluate(body, pts))
check('...which can be multiplied and is still a field', kind_of(fb * 3) == 'field' and kind_of(multiply_fields(body, 3)) == 'field')

# A point is its position wherever one is asked for
p = point(2, 3, 4)
base = box_exact((0, 0, 0), (4, 4, 4))
same = [
    ('sphere', sphere(2, p), sphere(2, (2, 3, 4))),
    ('move', move(base, p), move(base, (2, 3, 4))),
    ('distance_to_point', distance_to_point(p), distance_to_point((2, 3, 4))),
    ('distance_to_line', distance_to_line(p, (0, 0, 1)), distance_to_line((2, 3, 4), (0, 0, 1))),
    ('distance_to_plane', distance_to_plane(p, (0, 0, 1)), distance_to_plane((2, 3, 4), (0, 0, 1))),
    ('radial_field', radial_field(p), radial_field((2, 3, 4))),
    ('cylinder_z', cylinder_z(2, 8, p), cylinder_z(2, 8, (2, 3, 4))),
]
for name, with_point, with_tuple in same:
    check('a Point in %s is its tuple' % name, evaluate(with_point, pts) == evaluate(with_tuple, pts))

# What the model tree is told
source = '''
from fieldes import *
anchor = point(1, 2, 3)
base = box_exact((0, 0, 0), (4, 4, 4))
d = distance_to_point((5, 5, 5))
c = circle(3, (20, 50))
s = sphere(3, (1, 2, 3))
sc = scale_xyz(base, (2, 2, 2))
box = box_centered((4, 4, 4), (1, 1, 1))
half = half_space((0, 0, 1), (0, 0, 5))
ar = array_x(base, 3, 12)
th = offset(base, 2.5)
d
distance_to_point((9, 9, 9))
'''
out = runner.run(source)
scene = json.loads(app_support.scene_json(source, runner.last_globals, out))
by = {it['var']: it for it in scene['items'] if 'var' in it}
check('distance_to_point((5, 5, 5)) has a position to take a point', [x['label'] for x in by['d']['points']] == ['p'], by['d'].get('points'))
check('a flat shape\'s position has two coordinates', by['c']['points'][0].get('dims') == 2, by['c'].get('points'))
check('a size, a direction and a scale are not positions',
      [x['label'] for x in by['box']['points']] == ['center'] and [x['label'] for x in by['half']['points']] == ['point'] and
      by['sc']['points'] == [], [by[k].get('points') for k in ('box', 'half', 'sc')])
check('a count is marked: a field cannot take its place',
      [(n['label'], bool(n.get('discrete'))) for n in by['ar']['numbers']] == [('nx', True), ('dx', False)], by['ar']['numbers'])
check('the radius is no count', not by['s']['numbers'][0].get('discrete') and not by['th']['numbers'][0].get('discrete'))
check('a field that is only displayed is not drawn and has no eye',
      by['d'].get('displayable') is False)
fields = dict(app_support.field_sources())
check('the section viewer is given the fields of the script: the named ones and the displayed ones',
      'd' in fields and any(k.startswith('line:') for k in fields), list(fields))

# The menu writes simulations on the body it is given
req = json.dumps({'kind': 'operation', 'name': 'static_analysis', 'body': 'plate', 'lo': [0, 0, 0], 'hi': [40, 20, 10]})
code = app_support.menu_call(req)
check('a simulation is written with the body in it, twice', code.count('plate') == 2 and code.startswith('static_analysis(plate, '), code)
tree = ast.parse(code, mode='eval')
check('...with a support and a load on its lower and upper end', 'fixed(box_exact((-0.4, -0.4, -0.4), (40.4, 20.4, 1)))' in code and
      'force(box_exact((-0.4, -0.4, 9), (40.4, 20.4, 10.4)), (0, 0, -100))' in code, code)

print('FAILED %d' % len(failures) if failures else 'ALL OK')
