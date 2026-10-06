# What the model tree is told: types, roles, owners (nesting), inputs (what a drop can replace or add), displayable
import json
import sys

from fieldes import *
from fieldes import runner, app_support

failures = []


def check(label, ok, detail=''):
    print(('ok    ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail != '' else ''))
    if not ok:
        failures.append(label)


source = '''
from fieldes import *
base = box_exact((0, 0, 0), (40, 30, 6))
hole = cylinder_z(4, 20, (20, 15, -5))
plate = difference(base, hole)
thick = offset(plate, 1.0)
thick
ball = sphere(8, (60, 15, 10))
ball
anchor = point(60, 15, 25)
reach = distance_to_point(anchor)
rounded = offset(ball, ramp(reach, (0, 40), (3.0, 0.5)))
rounded
combined = union(thick, ball, rounded)
combined
mat = Material('m', 70e3, 0.3, 2.7e-9)
disc = circle(6, (20, 50))
sheet = plane((0, 0, 3), (0, 0, 1))
'''
out = runner.run(source)
scene = json.loads(app_support.scene_json(source, runner.last_globals, out))
by = {it['var']: it for it in scene['items'] if 'var' in it}

check('a 3D shape, a 2D shape, a field, a surface, a point, a material',
      [by[v]['type'] for v in ('plate', 'disc', 'reach', 'sheet', 'anchor', 'mat')] ==
      ['solid', 'profile', 'field', 'surface', 'point', 'conditions'],
      [by[v]['type'] for v in ('plate', 'disc', 'reach', 'sheet', 'anchor', 'mat')])
check('a primitive has no inputs, an operation has', by['base']['role'] == 'primitive' and by['plate']['role'] == 'operation')
check('a model is nested under the FIRST statement that uses it',
      by['base'].get('owner') == 'shape:plate' and by['hole'].get('owner') == 'shape:plate' and
      by['plate'].get('owner') == 'shape:thick' and by['anchor'].get('owner') == 'shape:reach' and
      by['reach'].get('owner') == 'shape:rounded' and by['ball'].get('owner') == 'shape:rounded',
      {k: v.get('owner') for k, v in by.items()})
check('...and a later user does not take it away', by['thick'].get('owner') == 'shape:combined' and
      by['rounded'].get('owner') == 'shape:combined')
check('what nobody uses has no owner', 'owner' not in by['combined'] and 'owner' not in by['disc'])
check('a material cannot be drawn', by['mat'].get('displayable') is False and by['plate'].get('displayable') is not False)

ins = by['plate']['inputs']
check('inputs of difference(base, hole): the names and where they are', [i['name'] for i in ins] == ['base', 'hole'] and
      all(len(i['span']) == 4 for i in ins), [i['name'] for i in ins])
check('offset takes one model; union and difference take any number', not by['thick']['variadic'] and by['combined']['variadic'] and by['plate']['variadic'])
deep = [i for i in by['rounded']['inputs'] if i.get('deep')]
check('a model deeper in the call can be replaced too (offset(ball, ramp(reach, ...)))',
      [i['name'] for i in by['rounded']['inputs'] if not i.get('deep')] == ['ball'] and [i['name'] for i in deep] == ['reach'],
      by['rounded']['inputs'])
nums = by['thick']['numbers']
check('the numbers of offset(plate, 1.0): the one number, its text, its place', len(nums) == 1 and nums[0]['text'] == '1.0' and
      len(nums[0]['span']) == 4, nums)
box_nums = by['base']['numbers']
check('the numbers of box_exact((0, 0, 0), (40, 30, 6)): six, each named by its place in the points',
      len(box_nums) == 6 and [n['text'] for n in box_nums] == ['0', '0', '0', '40', '30', '6'] and
      box_nums[5]['label'].endswith('[2]'), [(n['label'], n['text']) for n in box_nums])
check('a model written as a name is not a number', all(n['text'] not in ('plate', 'ball') for n in by['rounded']['numbers']))
check('the kinds table has the colours', {'solid', 'field', 'point'} <= set(scene['kinds']) and
      scene['kinds']['solid']['color'].startswith('#'))
check('a displayed variable is visible, others are not', by['thick']['visible'] and not by['plate']['visible'])

print('FAILED: ' + ', '.join(failures) if failures else 'ALL OK')
sys.exit(1 if failures else 0)
