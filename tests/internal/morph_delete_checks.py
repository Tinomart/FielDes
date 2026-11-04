''' Deleting a point of a body of points (the user, 2026-11-02, a picture of "a face is the numbers of three or four different points (0 to 7): (2, 6, 8)" after he deleted the ninth point of a morph polyhedron):
    the model tree takes the point out of the list of the call (app_support.arg_edits), and the faces= of the call lose the faces that name it, the hole is closed by one face, the numbers are renumbered.  A cube
    whose bottom square was pressed out into a ninth point (four triangles round it) is a cube again when that point is deleted.  Run like the other internal checks (the library on the path, FIELDES_DIR set) '''
import ast
import json
import sys

from fieldes import app_support

failures = []


def check(label, ok):
    print(('ok    ' if ok else 'FAIL  ') + label)
    if not ok:
        failures.append(label)


def faces_of(source, var='body'):
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id == var:
            pass
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id == var + '_faces':
            return [tuple(f) for f in ast.literal_eval(ast.get_source_segment(source, node.value))]
    return None


def apply(source, line, var, names):
    answer = json.loads(app_support.arg_edits(json.dumps({'source': source, 'statements': [{'var': var, 'line': line, 'remove': names}]})))
    out = answer['statements'][0]
    lines = source.split('\n')
    lines[out['line']:out['end_line'] + 1] = out['text'].split('\n')
    return '\n'.join(lines)


points = ['p%d = point(%s)' % (k + 1, c) for k, c in enumerate(['0, 0, 0', '10, 0, 0', '10, 10, 0', '0, 10, 0', '0, 0, 10', '10, 0, 10', '10, 10, 10', '0, 10, 10', '5, 5, -4'])]
source = '\n'.join(points) + '''
body_faces = [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7), (4, 5, 6, 7), (0, 1, 8), (1, 2, 8), (2, 3, 8), (3, 0, 8)]
body = morph_polyhedron([p1, p2, p3, p4, p5, p6, p7, p8, p9])
body
'''
line = source.split('\n').index(next(l for l in source.split('\n') if l.startswith('body ='))) + 1

# the ninth point is deleted: the square under it is back
after = apply(source, line, 'body', ['p9'])
faces = faces_of(after)
check('the list no longer holds p9: %s' % after.split('\n')[line - 1][:70], 'p9' not in after.split('body = ')[1].split('\n')[0] or 'p9]' not in after)
check('the cube has its six faces again (%d)' % len(faces), len(faces) == 6)
check('the bottom is one square', any(sorted(f) == [0, 1, 2, 3] for f in faces))
check('no face names a point that is not there', all(0 <= v < 8 for f in faces for v in f))

# a corner of the cube is deleted (p2, in the middle of the list): the hole round it pinches at the pressed-out point's triangles, it is no closed loop, so the faces are the hull's again and the list is shorter
after = apply(source, line, 'body', ['p2'])
check('a corner deleted: the point is out of the list and the faces are the hull\'s again (body_faces = None)', 'p2' not in after.split('body = ')[1].split('\n')[0] and 'body_faces = None' in after)

# a hole that is not a closed loop: the faces are the hull's
open_source = source.replace('(0, 1, 8), (1, 2, 8), (2, 3, 8), (3, 0, 8)]', '(0, 1, 8), (1, 2, 8)]')
after = apply(open_source, line, 'body', ['p9'])
check('a hole that is not a closed loop gives the hull\'s faces (body_faces = None)', 'body_faces = None' in after)

# THE LINES STAY WHERE THEY ARE (the user, 2026-11-03: after deleting a point the closing bracket of the list and its end mark were gone and the end of the point's statement was left behind).  The model tree
# (ScenePanel::deleteItems, wireStage) writes the lines of the answer of arg_edits back to the lines `line` .. `end_line` one to one (a line the answer has no text for goes, text beyond the last line is cut off) and
# only THEN cuts the lines of the deleted point's own statement at the numbers they had.  This does the same to a script with the faces list, its marks and the point's statement between the list and the call
GONE = '\x01gone'


def delete_like_the_tree(src, var, call_line, name):
    answer = json.loads(app_support.arg_edits(json.dumps({'source': src, 'statements': [{'var': var, 'line': call_line, 'remove': [name]}]})))['statements'][0]
    lines = src.split('\n')
    text = answer['text'].split('\n')
    span = answer['end_line'] - answer['line'] + 1
    staged = list(lines)
    for i in range(span):
        staged[answer['line'] + i] = text[i] if i < len(text) else GONE
    cut = []
    for node in ast.parse(src).body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id == name:
            cut += list(range(node.lineno - 1, node.end_lineno))
        if isinstance(node, ast.Expr) and isinstance(node.value, ast.Name) and node.value.id == name:
            cut += list(range(node.lineno - 1, node.end_lineno))
    for k in cut:
        staged[k] = GONE
    return '\n'.join(l for l in staged if l != GONE), len(text) <= span


real = '\n'.join(points[:8]) + '''
# fieldes-internal: begin (the faces of the body below: the program writes and reads them, they are no part of the model)
body_faces = [
    (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7), (4, 5, 6, 7), (0, 1, 8), (1, 2, 8), (2, 3, 8),
    (3, 0, 8),
]
# fieldes-internal: end
p9 = point(
    5, 5, -4,
    influence=var(1),
)
p9
body = morph_polyhedron([p1, p2, p3, p4, p5, p6, p7, p8, p9])
body
'''
real_line = real.split('\n').index('body = morph_polyhedron([p1, p2, p3, p4, p5, p6, p7, p8, p9])') + 1
result, fits = delete_like_the_tree(real, 'body', real_line, 'p9')
try:
    ast.parse(result)
    parses = True
except SyntaxError:
    parses = False
check('the point and its statement are gone and the script still parses: the text does not hold more lines than the lines it replaces', fits and parses)
if parses:
    list_faces = faces_of(result)
    check('the marks and the list are whole after the delete (begin, the list closed by its bracket, end): %s' % [l for l in result.split('\n') if 'fieldes-internal' in l or l.strip() == ']'],
          result.count('# fieldes-internal: begin') == 1 and result.count('# fieldes-internal: end') == 1 and list_faces is not None and len(list_faces) == 6
          and 'p9' not in result and 'point(\n    5, 5, -4' not in result)

print('%d checks failed' % len(failures) if failures else 'all passed')
sys.exit(1 if failures else 0)
