'''
Checks that every primitive of the menus has a picture of its own (the user, 2026-11-01: "not all new parametric shapes and such have their own unique icons.  They should.  And every new shape should
always get its own unique icon.  And those are obviously also always shown in the model tree").  The pictures are painted in app/src/typeicons.cpp, by the name of the primitive (drawShape,
drawCondition, drawMaterial; a cell by its kind, drawCell); the model tree takes the name from the function a statement calls (outline._glyph_of).  A primitive that has no block of its own there
would be drawn with the picture of its kind, the same as every other primitive of that kind -- this check fails for it, so a new shape cannot be added to the menus without its picture.
Library check, no window: it reads the source of the pictures.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import ast
import os
import re
import sys

from fieldes import menu_catalog as M
from fieldes import outline

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


here = os.path.dirname(os.path.abspath(__file__))
source = os.path.join(here, '..', '..', 'app', 'src', 'typeicons.cpp')
text = open(source, encoding='utf-8').read()

# the names that have a block of their own: `name == "x"`, a cell by `kind == "x"` (the name without cell_), the metals of drawMaterial by the table that tells their element
painted = set(re.findall(r'name == "([A-Za-z0-9_]+)"', text))
painted |= set('cell_' + k for k in re.findall(r'kind == "([A-Za-z0-9_]+)"', text))
table = re.search(r'metals\s*=\s*\{(.*?)\};', text, re.S)
painted |= set(re.findall(r'\{"([A-Za-z0-9_]+)",\s*"[A-Za-z]+"\}', table.group(1))) if table else set()

# the primitives whose picture is the picture of their KIND, which no other primitive uses: the kind's picture is painted in drawKind, by its type (and domain)
kind_picture = {
    'point': r'type == "point"',
    'symmetry_planes': r'type == "symmetry"',
    'material': r'type == "material"',
    'manufacturing_constraint': r'type == "conditions" && domain == "manufacturing"',
}

names = [n for n, _group, _template in M.PRIMITIVES]
check('the menus list primitives', len(names) > 60, len(names))
check('no primitive is listed twice', len(set(names)) == len(names), sorted(n for n in set(names) if names.count(n) > 1))

lack = []
for n in names:
    if n in painted:
        continue
    if n in kind_picture and re.search(kind_picture[n], text):
        continue
    lack.append(n)
check('every primitive of the menus has a picture of its own (' + str(len(names)) + ' primitives)', not lack, ', '.join(lack))

# a kind picture is the picture of one primitive only: a primitive that has the picture of its kind has no painted block as well
shared = [n for n in kind_picture if n in painted]
check('the primitives that have the picture of their kind do not also have a painted block', not shared, ', '.join(shared))

# the model tree shows the picture: the function a statement calls is the name the tree takes (a cell by its kind, a material or a fluid of the library by its name)
outline._PRIMITIVE_NAMES = None
tree_lack = [n for n in names if not n.startswith('cell_') and outline._glyph_of(n, None) != n]
check('the model tree takes the picture of each primitive from the function that makes it', not tree_lack, ', '.join(tree_lack))
cells = [n for n in names if n.startswith('cell_')]
cell_lack = [n for n in cells if outline._glyph_of('cell_periodic', ast.parse("cell_periodic('%s')" % n[5:]).body[0].value) != n]
check('a cell is told by its kind (' + str(len(cells)) + ' cells)', not cell_lack, ', '.join(cell_lack))

# the pictures that are painted for a name that is no primitive of the menus (half_space ...): listed, not an error
others = sorted(n for n in painted if n not in set(names))
print('painted for a name that is no primitive of the menus: ' + (', '.join(others) or '-'))

# the names that share one block (their picture is told apart inside it): listed
for line in re.findall(r'if \((name == "[A-Za-z0-9_]+"(?: \|\| name == "[A-Za-z0-9_]+")+)\)', text):
    print('one block draws: ' + ', '.join(re.findall(r'"([A-Za-z0-9_]+)"', line)))

if bad:
    print('FAILED: ' + str(len(bad)) + ' check(s)')
    sys.exit(1)
print('ALL CHECKS PASSED')
