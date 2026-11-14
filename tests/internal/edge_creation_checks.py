'''
Checks of the edges that the commands of the right-click menu make (the user, 2026-11-03: a message that an edge was removed came the moment he made the lines along connected edges of a pyramid, and an edge must not be made twice).  Library checks, no window.  Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import json
from fieldes import *
from fieldes import runner
from fieldes import app_support

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


# --- H110: a lost edge is removed by the tree only when it was found before
def go(shape):
    return edge_at(shape, (10, 10, 0))


never = go(sphere(12))
check('an edge that never was found is lost and says so', bool(getattr(never, '_lost', None)))
check('... and it was not found before: the tree leaves it alone', getattr(never, '_lost_after_found', None) is False, getattr(never, '_lost_after_found', None))
good = go(box((20, 20, 20), position=(0, 0, 0)))
check('the same call on a box finds its edge', not getattr(good, '_lost', None))
after = go(sphere(12))
check('the same call on a sphere afterwards is lost', bool(getattr(after, '_lost', None)))
check('... and it was found before: the tree removes it', getattr(after, '_lost_after_found', None) is True, getattr(after, '_lost_after_found', None))

# --- H110 and H111: the connected edges of the pyramid of his picture, each written once, none a dud
pyramid_1 = pyramid(0.2, 0.2, position=(-0.1, -1.11, 1.23))
runner.last_globals = {'pyramid_1': pyramid_1, 'plate': box((20, 20, 20), position=(0, 0, 0))}


def ask(var, point, existing=None):
    request = {'var': var, 'point': list(point)}
    if existing is not None:
        request['existing'] = existing
    return json.loads(app_support.connected_edges(json.dumps(request)))


first = ask('pyramid_1', (-0.1, -1.01, 1.23))
places = [e[2] for e in first.get('edges', [])]
check('the connected edges of the pyramid are made: at least the four of its base', len(places) >= 4, len(places))
dud = 0
for place in places:
    line = edge_at(pyramid_1, tuple(place))
    dud += 1 if getattr(line, '_lost', None) else 0
check('... and every statement that is written finds its edge (no dud on creation)', dud == 0, (dud, len(places)))
check('... no edge twice', len(set(tuple(round(c, 4) for c in p) for p in places)) == len(places), places)
again = ask('pyramid_1', (-0.1, -1.01, 1.23), existing=[places[0]])
check('with one of its edges in the script it is not made again', len(again.get('edges', [])) == len(places) - 1 and again.get('existing') == 1, (len(again.get('edges', [])), again.get('existing')))
every = ask('pyramid_1', (-0.1, -1.01, 1.23), existing=places)
check('with all of its edges in the script nothing is made, and it says why', every.get('edges') == [] and every.get('existing') == len(places), every)

# --- a box: twelve edges, then none
box_first = ask('plate', (10, 0, 10))
check('the box gives its twelve edges', len(box_first.get('edges', [])) == 12, len(box_first.get('edges', [])))
box_places = [e[2] for e in box_first.get('edges', [])]
box_again = ask('plate', (10, 0, 10), existing=box_places[:5])
check('... with five of them in the script, the seven that are left', len(box_again.get('edges', [])) == 7, len(box_again.get('edges', [])))

# --- one edge: the click on an edge that is in the script
def exists(var, point, existing):
    return json.loads(app_support.edge_exists(json.dumps({'var': var, 'point': list(point), 'existing': existing})))


check('a click on an edge that a statement has says which statement', exists('plate', (10, 0, 10), [[0, 10, -10], box_places[0], [10, 0, 10]]).get('index') is not None)
check('a click on an edge that no statement has says nothing', exists('plate', (10, 0, 10), [[0, 10, -10], [-10, 0, 10]]) == {})
# --- H113: the memory of found edges is of the statement, not of the place it writes (the user, 2026-11-04, his file edge_notdeleted..py: placed the edge, dragged the handles, it got lost, it was not removed).
# While an edge follows a drag the program writes the place it is at now into the script; the run after that has a place no run found before
def go_at(shape, near):
    return edge_at(shape, near)


found_first = go_at(box((20, 20, 20), position=(0, 0, 0)), (10, 9.7, 5.0))
check('H113: the call finds its edge at a place a little off the one it writes', not getattr(found_first, '_lost', None))
lost_after_rewrite = go_at(sphere(12), (10.0, 10.0, 5.2))
check('H113: lost at the place the program wrote for it (a place no run found before): it is lost', bool(getattr(lost_after_rewrite, '_lost', None)))
check('H113: ... and it was found before by this statement: the tree removes it', getattr(lost_after_rewrite, '_lost_after_found', None) is True, getattr(lost_after_rewrite, '_lost_after_found', None))

# --- H113 again (measured in the library and in the window, 2026-11-04): selecting a shape writes a handles(...) line above the call, so the call stands on another line, and the edge lost after that counted as
# 'never found' and stayed in the script.  The statement is its function and the name it gives its result, and the place it writes must be where the edge was last found.  Run the way the program runs a
# script: the file is called <file> and its text is runner.running_source
SCRIPT = '''from fieldes import *
{pad}s = sphere(2.5, position=(0.512616, -6, 1.6))
c = cylinder(2.5, 5, position=({cx}, {cy}, {cz} + 2.5))
d = difference(s, c)
edge_1 = edge_at(d, ({px}, {py}, {pz}))
'''


def as_the_program_runs(pad, cylinder_at, near=(0.012616, -6.0, 4.0494897)):
    text = SCRIPT.format(pad='# a handles(...) line, say\n' * pad, cx=cylinder_at[0], cy=cylinder_at[1], cz=cylinder_at[2], px=near[0], py=near[1], pz=near[2])
    runner.running_source = text
    try:
        scope = {}
        exec(compile(text, '<file>', 'exec'), scope)
    finally:
        runner.running_source = None
    return scope['edge_1']


placed = as_the_program_runs(0, (2.512616, -6.0, 2.0))
check('H113: a sphere minus a cylinder, the edge placed on their intersection: found', not getattr(placed, '_lost', None))
moved = as_the_program_runs(2, (2.51262, -3.0, -1.1))
check('H113: the cylinder moved away and two lines written above the call: the edge is lost', bool(getattr(moved, '_lost', None)))
check('H113: ... it is the same statement on another line, so the tree removes it', getattr(moved, '_lost_after_found', None) is True, getattr(moved, '_lost_after_found', None))
elsewhere = as_the_program_runs(0, (2.51262, -3.0, -1.1), near=(5.0, 5.0, 5.0))
check('H113: an edge of that name at another place, that never was found there, is left alone', bool(getattr(elsewhere, '_lost', None)) and getattr(elsewhere, '_lost_after_found', None) is False, getattr(elsewhere, '_lost_after_found', None))

# --- E2 (measured in the window and in the library, 2026-11-04): the two faces of an edge are the biggest two that turn against each other, not a cluster of samples that straddle the crease -- at the rim of the dent
# of a sphere in a cylinder the search found the sphere (35 samples), the cylinder (25) and a straddling cluster (12) whose normal turns most against the cylinder's, took that one for the second face, and the trace
# of the edge that started from it was lost at once
from fieldes.stdlib import features as F
dent = difference(cylinder(10, 40, position=(0, 0, 20)), sphere(14.8548, position=(8, 0, 20)))          # (a model stands by its position, its middle: the cylinder is from z = 0 to z = 40)
_near, _scale, _size, _values = F._prepare(dent, (6.9512, -7.1887, 28.259), None, None, 'edge_at')
_found = F._wider(lambda s: F.find_feature(_values, _near, s, _size, 'edge'), _scale, _size, False)
_planes = F._plane_set(F._Surface(_values, _size), _found['point'], max(min(_scale, 0.2 * _size), 2e-3 * _size), 2) if _found else None
check('E2: the two faces of the edge of a dent in a cylinder are the sphere and the cylinder, not a cluster that straddles them', _planes is not None and any(abs(p[0][2]) > 0.5 for p in _planes) and any(abs(p[0][2]) < 0.1 for p in _planes), _planes)
print('ALL CHECKS PASSED' if not bad else 'FAILED: ' + '; '.join(bad))
