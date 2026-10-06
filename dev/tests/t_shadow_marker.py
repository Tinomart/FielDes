# A Ctrl+drag of a model that nothing else uses writes `# shadow: name` in the call's statement: the statement holds a REFERENCE
# (a shadow row) and the model stays a top-level row, instead of moving under its first user
import json

from fieldes import *
from fieldes import runner, app_support

failures = []


def check(label, ok, detail=''):
    print(('ok    ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail != '' else ''))
    if not ok:
        failures.append(label)


def scene_of(source):
    out = runner.run(source)
    scene = json.loads(app_support.scene_json(source, runner.last_globals, out))
    return {it['var']: it for it in scene['items'] if 'var' in it}


head = '''from fieldes import *
a = sphere(3, (0, 0, 0))
b = sphere(3, (10, 0, 0))
e = sphere(3, (40, 0, 0))
'''
plain = scene_of(head + 'u = union(a, b, e)\nu\ne\n')
check('without the mark, the first user owns the model', plain['e'].get('owner') == 'shape:u', plain['e'].get('owner'))

marked = scene_of(head + 'u = union(a, b, e)  # shadow: e\nu\ne\n')
check('with the mark, the model stays where it is (no owner) and the call lists it', 'owner' not in marked['e'] and
      marked['u'].get('shadows') == ['e'] and 'e' in marked['u']['deps'], (marked['e'].get('owner'), marked['u'].get('shadows')))
check('the other models of the call are still its own', marked['a'].get('owner') == 'shape:u' and marked['b'].get('owner') == 'shape:u')

# the mark does not make a later plain user a shadow: the first unmarked user owns the model
later = scene_of(head + 'u = union(a, b, e)  # shadow: e\nu\nv = union(e, a)\nv\n')
check('a later user that is not marked owns it', later['e'].get('owner') == 'shape:v', later['e'].get('owner'))

# two names, and a mark inside a multi-line statement
two = scene_of(head + 'c = sphere(3, (50, 0, 0))\nu = union(a,\n          e,\n          c)  # shadow: e, c\nu\n')
check('several names, in a statement of several lines', 'owner' not in two['e'] and 'owner' not in two['c'] and
      two['u'].get('shadows') == ['c', 'e'], two['u'].get('shadows'))

# renaming the model renames it in the mark too
source = head + 'u = union(a, b, e)  # shadow: e\nu\n'
edits = json.loads(app_support.rename_edits(json.dumps({'source': source, 'old': 'e', 'new': 'f'})))['edits']
lines = source.split('\n')
for line, c0, c1 in reversed(edits):
    lines[line] = lines[line][:c0] + 'f' + lines[line][c1:]
check('a rename changes the name in the mark too', '\n'.join(lines).count('shadow: f') == 1 and ' e' not in '\n'.join(lines).split('shadow:')[0].split('union')[1],
      '\n'.join(lines))

# changing the call's arguments keeps the comment
request = {'source': source, 'statements': [{'var': 'u', 'line': 5, 'remove': ['b'], 'insert': []}]}
answer = json.loads(app_support.arg_edits(json.dumps(request)))['statements'][0]
check('arg_edits keeps the comment at the end of the statement', '# shadow: e' in answer['text'], answer['text'])

print('FAILED %d' % len(failures) if failures else 'ALL OK')
