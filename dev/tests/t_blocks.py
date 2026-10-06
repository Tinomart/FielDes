# Custom blocks (fieldes.blocks): the files of a folder become functions in every script; hot reload; errors; the menus' view of them
import json
import os
import shutil
import sys
import time

folder = os.path.join(os.environ.get('TEMP', '.'), 'fieldes_blocks_test')
shutil.rmtree(folder, ignore_errors=True)
os.makedirs(folder)
os.environ['FIELDES_BLOCKS'] = folder

from fieldes import *
from fieldes import blocks, runner, app_support

failures = []


def check(label, ok, detail=''):
    print(('ok    ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail != '' else ''))
    if not ok:
        failures.append(label)


def write(name, text):
    with open(os.path.join(folder, name), 'w', encoding='utf-8') as f:
        f.write(text)
    # (the file system's clock may be coarse: make sure the change is seen)
    t = time.time() + write.tick
    write.tick += 2
    os.utime(os.path.join(folder, name), (t, t))


write.tick = 0

write('a.py', "def ring_of(body, n=3):\n    ''' Copies a body round the z axis '''\n    return array_polar_z(body, n)\n\n"
              "def ball(radius=4.0):\n    ''' A ball '''\n    return sphere(radius)\n\n"
              "def _hidden():\n    return 1\n")
write('b.py', "def thick_ring(body, n=4):\n    ''' A ring of bodies, grown by a millimetre '''\n    return offset(ring_of(body, n), 1.0)\n")
write('_private.py', "def should_not_be_there():\n    return 1\n")
write('bad.py', "def broken(:\n    pass\n")
write('clash.py', "def union(a, b):\n    return a\n")

names = blocks.names()
check('the functions of the good files are blocks', {'ring_of', 'ball', 'thick_ring'} <= set(names), names)
check('a name that starts with an underscore is not one', '_hidden' not in names and 'should_not_be_there' not in names)
errs = dict((os.path.basename(p), why) for p, why in blocks.errors())
check('a file with a syntax error is reported, the rest works', 'bad.py' in errs and 'SyntaxError' in errs['bad.py'], errs.get('bad.py'))
check('a block with the name of a library function is refused', 'clash.py' in errs and 'union' in errs['clash.py'], errs.get('clash.py'))
check('a block may use the blocks of the files before it', True)

# in a script
out = runner.run("r = thick_ring(ball(3), 5)\nr\nb = ball()\nb")
check('a script calls a block, and a block calls another', len(out) == 4)
check('the problems of a block file are in the script\'s output',
      'custom block file bad.py' in runner.last_output and 'custom block file clash.py' in runner.last_output,
      runner.last_output.strip().replace('\n', ' | '))

# the model tree's view of it
scene = json.loads(app_support.scene_json("r = thick_ring(ball(3), 5)\nr\nb = ball()\nb", runner.last_globals, out))
items = {it['var']: it for it in scene['items'] if 'var' in it}
check('the tree marks a block call', items['r'].get('block') == 'thick_ring' and items['b'].get('block') == 'ball', {k: v.get('block') for k, v in items.items()})
check('its type is that of what it makes', items['r'].get('type') == 'solid', items['r'].get('type'))

# what the menus see
info = {b['name']: b for b in blocks.info()}
check('an operation block: first argument the model, defaults for the rest', info['ring_of']['operation'] and not info['ring_of']['primitive'])
check('a primitive block: nothing required', info['ball']['primitive'] and not info['ball']['operation'])
check('the first line of the docstring is kept', info['ring_of']['doc'] == 'Copies a body round the z axis', info['ring_of']['doc'])
recs = blocks.records()
check('completion records: a definition and a call tip for each', any(r.startswith('def\tball\t') for r in recs) and any(r.startswith('tip\tball\tball(radius=4.0)') for r in recs))
check('the completion info of the editor has them', any(r.startswith('def\tball\t') for r in app_support.completion_info()))

# hot reload
before = blocks.refresh()
check('nothing changed: no reload', before is False)
write('a.py', "def ring_of(body, n=3):\n    ''' Copies a body round the z axis, twice '''\n    return array_polar_z(body, 2 * n)\n\n"
              "def ball(radius=4.0):\n    return sphere(radius * 2)\n")
check('a changed file is read again', blocks.refresh() is True)
b = runner.run("b = ball(1)\nb")[1]
check('the new version runs', abs(evaluate(b, (2, 0, 0))) < 1e-4, evaluate(b, (2, 0, 0)))
check('and the docstring is the new one', {b['name']: b for b in blocks.info()}['ring_of']['doc'].endswith('twice'))
os.remove(os.path.join(folder, 'b.py'))
check('a removed file takes its blocks away', 'thick_ring' not in blocks.names(), blocks.names())

# blocks can return anything
write('c.py', "def a_point(x=1.0):\n    return point(x, 2, 3)\n\ndef a_field(k=2.0):\n    return k * z_field()\n\n"
              "def a_material():\n    return Material('mine', 100000, 0.3)\n")
out = runner.run("p = a_point(5)\nf = a_field()\nm = a_material()")
scene = json.loads(app_support.scene_json("p = a_point(5)\nf = a_field()\nm = a_material()", runner.last_globals, out))
types = {it['var']: it.get('type') for it in scene['items'] if 'var' in it}
check('a block may make a point, a field, a material', types == {'p': 'point', 'f': 'field', 'm': 'conditions'}, types)

shutil.rmtree(folder, ignore_errors=True)
print('FAILED: ' + ', '.join(failures) if failures else 'ALL OK')
