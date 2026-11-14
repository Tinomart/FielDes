''' H216: ctrl+click on a keyword argument -- the resolver of the editor (python/fieldes/app_support.py find_definition), library only, no window '''
import os
import sys

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))        # (the repository: tests/internal/ is two folders in)
sys.path.insert(0, os.path.join(R, 'python'))
from fieldes import app_support as A

sys.stdout.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)
failures = []


def check(label, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + ('' if ok else '   ' + str(detail)))
    if not ok:
        failures.append(label)


script = os.path.join(R, 'scratch_h216_script.py')          # (never written: the text is given to the resolver)
text = '''from fieldes import *

mesh_1 = mesh_tet4(
    torus_1,
    element_size=None,
    smooth=3,
    boundary_layers=None,
)
x = 5
def local(a, b=2):
    return a + b
local(1,
      b=3)
mesh_2 = mesh_tet4(part, 1.0, smooth = 3)
y = box(1, position=(0, 0, 0))
'''
lines = text.split('\n')


def where(name, line_text_start, occurrence=0):
    for i, l in enumerate(lines):
        if l.strip().startswith(line_text_start):
            col = l.index(name)
            return i, col
    raise KeyError(line_text_start)


def ask(name, line0, col0, owner_marker=True):
    owner = '@%d:%d' % (line0, col0) if owner_marker else ''
    return A.find_definition('\x1f'.join([name, owner, script, text]))


i, c = where('smooth', "smooth=3")
got = ask('smooth', i, c)
file, line = got.split('\t') if got else ('', '0')
src = open(os.path.join(R, 'python', 'fieldes', 'stdlib', 'meshing.py'), encoding='utf-8').read().split('\n')
check('smooth= of mesh_tet4( ... ) on its own line jumps into meshing.py', file.endswith('meshing.py'), got)
check('... to the line of the parameter smooth in the def of mesh_tet4 ({!r})'.format(src[int(line) - 1][:90]), "def mesh_tet4(" in ''.join(src[int(line) - 3:int(line) + 1]) or "smooth=None" in src[int(line) - 1], got)
i, c = where('boundary_layers', 'boundary_layers=None')
got2 = ask('boundary_layers', i, c)
check('boundary_layers= (a later line of the same call) jumps to the same def, a later parameter line or the same line', got2.endswith('meshing.py\t' + line) or got2.split('\t')[0].endswith('meshing.py'), got2)
i, c = where('b', 'b=3')
got3 = ask('b', i, c)
check('b= of a call of a function of the script itself jumps to its parameter b in the script ({})'.format(got3), got3.split('\t')[0] == script and int(got3.split('\t')[1]) == 10, got3)
i, c = where('smooth', 'mesh_2 = ')
c = lines[i].index('smooth')
got4 = ask('smooth', i, c)
check('smooth = (with spaces round the =) inside the call on one line jumps too', got4.split('\t')[0].endswith('meshing.py'), got4)
i, c = where('position', 'y = box(')
c = lines[i].index('position')
got5 = ask('position', i, c)
check('position= of box( ... ) jumps into the library (the def of box, or its parameter)', got5 != '' and got5.split('\t')[0].endswith('.py'), got5)
i, c = where('x', 'x = 5')
got6 = ask('x', i, c)
check('x = 5 (an assignment, not a keyword) is resolved as a name as before: its own line ({})'.format(got6), got6.split('\t')[0] == script and int(got6.split('\t')[1]) == 9, got6)
check('the function name itself still jumps (mesh_tet4)', A.find_definition('\x1f'.join(['mesh_tet4', '', script, text])).split('\t')[0].endswith('meshing.py'))
print('=' * 60)
print('{} check(s) failed'.format(len(failures)) if failures else 'ALL CHECKS PASSED', failures)
sys.exit(1 if failures else 0)
