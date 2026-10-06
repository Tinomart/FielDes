# Renaming a variable from the model tree: where the name is, and where it must not be touched
import json
import sys

from fieldes import *
from fieldes import app_support

failures = []


def check(label, ok, detail=''):
    print(('ok    ' if ok else 'FAIL  ') + label + (('   ' + str(detail)) if detail != '' else ''))
    if not ok:
        failures.append(label)


def rename(source, old, new):
    answer = app_support.rename_edits(json.dumps({'source': source, 'old': old, 'new': new}))
    edits = json.loads(answer)['edits']
    lines = source.split('\n')
    for line, c0, c1 in sorted(edits, reverse=True):
        lines[line] = lines[line][:c0] + new + lines[line][c1:]
    return '\n'.join(lines)


def refused(source, old, new):
    try:
        rename(source, old, new)
        return None
    except ValueError as e:
        return str(e)


src = '''from fieldes import *
plate = box_exact((0, 0, 0), (40, 30, 6))
plate = handles(plate, move=(var(0), var(0), var(0)))
hole = cylinder_z(4, 20, (20, 15, -5))
drilled = difference(plate, hole)
drilled
# hidden: plate
print("plate", plate)
x = foo(plate=1, other=plate)
y = plate.volume + plate
'''
out = rename(src, 'plate', 'base')
check('every use of the name is renamed (definition, handles, difference, show, print, keyword value, attribute owner)',
      out.count('base') == 9 and 'plate' in out, out.count('base'))
check('a string is not touched', 'print("plate", base)' in out)
check('a keyword argument of the same name is not touched', 'foo(plate=1, other=base)' in out)
check('an attribute of the same name would not be touched', 'base.volume + base' in out)
check('the hidden comment line is renamed', '# hidden: base' in out)
check('the other names stay', 'hole' in out and 'drilled' in out)

fn = '''a = 1
def f(a):
    return a + 1
def g():
    return a * 2
def h():
    a = 5
    return a
b = [a for a in range(3)]
c = [a for q in range(3)]
'''
out = rename(fn, 'a', 'z')
check('a parameter of the same name is another variable', 'def f(a):\n    return a + 1' in out)
check('a function that uses the module variable is renamed inside', 'return z * 2' in out)
check('a function that assigns its own is left alone', '    a = 5\n    return a' in out)
check('a comprehension variable is its own', 'b = [a for a in range(3)]' in out and 'c = [z for q in range(3)]' in out)

check('a new name that is no name is refused', refused(src, 'plate', '2bad') is not None and refused(src, 'plate', 'a b') is not None)
check('a keyword is refused', refused(src, 'plate', 'class') is not None)
check('a name the script uses already is refused', 'already' in (refused(src, 'plate', 'hole') or ''), refused(src, 'plate', 'hole'))
check('the name of a function of the library is refused', 'library' in (refused(src, 'plate', 'sphere') or ''), refused(src, 'plate', 'sphere'))
check('the same name changes nothing', json.loads(app_support.rename_edits(json.dumps({'source': src, 'old': 'plate', 'new': 'plate'})))['edits'] == [])
check('a name that is not used has no edits', json.loads(app_support.rename_edits(json.dumps({'source': src, 'old': 'nothing', 'new': 'something'})))['edits'] == [])
u = "café = 1\nr = café + café\n"
check('columns are characters, not bytes', rename(u, 'café', 'tea') == 'tea = 1\nr = tea + tea\n')

print('FAILED: ' + ', '.join(failures) if failures else 'ALL OK')
sys.exit(1 if failures else 0)
