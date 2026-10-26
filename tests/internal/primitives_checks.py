'''
Checks of the primitives of the user's own (python/fieldes/user_primitives.py): a model of a script becomes a FILE of its own -- a function in the folder `primitives` of the user's data folder, read
as a block --, a menu pick writes ONE call of it, it is renamed and removed as a file, and the entries an older primitives.json held (the statements pasted in it) are written as files once.

Run it with the kernel's library found:    FIELDES_DIR=<folder of fieldes.dll> PYTHONPATH=python python tests/internal/primitives_checks.py
It uses a folder of its own for the user's data (never the user's), prints PASS or FAIL for every check, and exits with 1 when one failed (a few seconds).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import json
import os
import shutil
import sys
import tempfile
import traceback

DATA = tempfile.mkdtemp(prefix='fieldes_primitives_')
os.environ['FIELDES_USER_DIR'] = DATA              # (before anything reads it)

from fieldes import *
from fieldes import blocks, menu_catalog, user_primitives

failures = []


def check(label, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + detail) if detail else ''))
    sys.stdout.flush()
    if not ok:
        failures.append(label)


def inside(shape, p):
    return evaluate(shape, p) < 0.0


def manage(**request):
    return json.loads(user_primitives.manage(json.dumps(request)))


SOURCE = '''from fieldes import *
size = 10
base = box((0, 0, 0), (size, size, 2))
post = box((0, 0, 0), (2, 2, size))
bracket_src = union(base, post, position=(var(0), var(0), var(0)), rotation=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
bracket_src
'''
PLACED = '''from fieldes import *
cube = box((0, 0, 0), (1, 1, 1), position=(5, 0, 0))
cube
'''


def text_of(name):
    with open(os.path.join(user_primitives.folder(), name + '.py'), encoding='utf-8') as f:
        return f.read()


try:
    print('--- add selection: the model is a file')
    done = manage(op='add', source=SOURCE, line=5, end_line=5, name='bracket x', type='solid')
    check('the model is made a primitive (no error)', done.get('ok') is True, str(done.get('error')))
    check('and a file of its own, bracket_x.py, is in the folder of primitives', os.path.isfile(os.path.join(DATA, 'primitives', 'bracket_x.py')))
    text = text_of('bracket_x')
    check('it holds the function, with the statements the model is made of', 'def bracket_x():' in text and 'size = 10' in text and 'base = box(' in text and 'post = box(' in text, text.replace('\n', ' | '))
    check('its numbers are plain numbers (no var), and the placement arguments at their defaults are gone', 'var(' not in text and 'position=' not in text and 'scale=' not in text and text.rstrip().endswith('return union(base, post)'),
          text.replace('\n', ' | '))
    check('what a script was not given: the kind of its model is the first line', text.startswith('#@ primitive: solid\n'))

    print('--- the file is a block: a script calls it')
    blocks.refresh()
    check('the script namespace has bracket_x', 'bracket_x' in blocks.namespace(), str(blocks.errors()))
    part = blocks.namespace()['bracket_x']()
    check('and it makes the model: the base, the post, and nothing else', inside(part, (5, 5, 1)) and inside(part, (1, 1, 8)) and not inside(part, (5, 5, 5)) and not inside(part, (5, 5, 3)))
    mine = [b for b in blocks.info() if b['name'] == 'bracket_x']
    check('it is a block of the user (not of the blocks folder)', len(mine) == 1 and mine[0]['user'] is True)
    check('and the menu of custom blocks does not list it twice', not any(b['name'] == 'bracket_x' for b in menu_catalog._blocks()))

    print('--- the menu: one line')
    entries = [p for p in json.loads(menu_catalog.catalog())['primitives'] if p['name'] == 'bracket_x']
    check('the menu catalog has it among the primitives of the 3D shapes', len(entries) == 1 and entries[0]['group'] == '3D' and entries[0]['primitive'] and entries[0].get('custom'), str(entries))
    call = menu_catalog.call(json.dumps({'kind': 'primitive', 'name': 'bracket_x', 'x': 0, 'y': 0, 'z': 0, 'scale': 10, 'var': 'bracket_x_1', 'source': SOURCE}))
    check('a pick writes ONE call: bracket_x()', call == 'bracket_x()', repr(call))

    print('--- a placement that is not the default stays')
    done = manage(op='add', source=PLACED, line=2, end_line=2, name='cube_at_5', type='solid')
    text = text_of('cube_at_5') if done.get('ok') else ''
    check('position=(5, 0, 0) is kept: it is where the model was put', done.get('ok') is True and 'position=(5, 0, 0)' in text, str(done.get('error')) + ' ' + text.replace('\n', ' | '))
    blocks.refresh()
    cube = blocks.namespace()['cube_at_5']()
    check('and the model is there', inside(cube, (5.5, 0.5, 0.5)) and not inside(cube, (0.5, 0.5, 0.5)))

    print('--- names that are taken')
    taken = manage(op='add', source=SOURCE, line=5, end_line=5, name='box', type='solid')
    check('a name of the library is refused', taken.get('ok') is False, str(taken))
    again = manage(op='add', source=SOURCE, line=5, end_line=5, name='bracket_x', type='solid')
    check('so is the name of a primitive that is there', again.get('ok') is False, str(again))

    print('--- rename and remove are the file')
    renamed = manage(op='rename', name='bracket_x', label='bracket y')
    check('renamed (no error)', renamed.get('ok') is True, str(renamed.get('error')))
    check('the file is bracket_y.py now, and its function', os.path.isfile(os.path.join(DATA, 'primitives', 'bracket_y.py')) and not os.path.exists(os.path.join(DATA, 'primitives', 'bracket_x.py'))
          and 'def bracket_y():' in text_of('bracket_y'))
    blocks.refresh()
    names = blocks.namespace()
    check('the namespace has bracket_y and not bracket_x', 'bracket_y' in names and 'bracket_x' not in names)
    gone = manage(op='remove', name='bracket_y')
    blocks.refresh()
    check('removed: the file is deleted and the name is gone', gone.get('ok') is True and not os.path.exists(os.path.join(DATA, 'primitives', 'bracket_y.py')) and 'bracket_y' not in blocks.namespace())

    print('--- an older primitives.json: its entries become files, once')
    shutil.rmtree(os.path.join(DATA, 'primitives'), ignore_errors=True)
    with open(os.path.join(DATA, 'primitives.json'), 'w', encoding='utf-8') as f:
        json.dump({'added': ['capsule'], 'removed': [], 'labels': {}, 'custom': [
            {'name': 'old_one', 'type': 'solid', 'head': 'r = var(4)', 'main': 'o', 'expr': 'sphere(r)', 'names': ['o', 'r'], 'doc': 'an older one'}]}, f)
    user_primitives._CACHE['stamp'] = None
    d = user_primitives.load()
    check('the choices are kept and the file is written', d['added'] == ['capsule'] and os.path.isfile(os.path.join(DATA, 'primitives', 'old_one.py')))
    with open(os.path.join(DATA, 'primitives.json'), encoding='utf-8') as f:
        saved = json.load(f)
    check('primitives.json does not hold `custom` any more', 'custom' not in saved and saved['added'] == ['capsule'], str(saved))
    blocks.refresh()
    old = blocks.namespace().get('old_one')
    check('and the old primitive works as a block (its var() is the number it had)', old is not None and inside(old(), (3, 0, 0)) and not inside(old(), (5, 0, 0)), text_of('old_one').replace('\n', ' | '))
except Exception:
    check('no exception', False, traceback.format_exc(limit=4).replace('\n', ' | '))
finally:
    shutil.rmtree(DATA, ignore_errors=True)

print('=' * 70)
print('ALL CHECKS PASSED' if not failures else '{} check(s) failed: {}'.format(len(failures), failures))
sys.exit(1 if failures else 0)
