'''
Checks of fillet and chamfer on edges (fieldes.stdlib.fields): the rounding of one edge leaves the edges beside it as they are, any number of edges are taken at once, and the menu writes every edge
that is selected.  Library checks, no window.  Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import sys
import time
import traceback

from fieldes import *
from fieldes import menu_catalog as mc

bad = []
start = time.time()


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def value(s, p):
    return evaluate(s, [p])[0]


def run(name, fn):
    try:
        fn()
    except Exception:
        traceback.print_exc()
        bad.append(name + ' raised')


def box_edges():
    b = box(20, position=(10, 10, 10))
    top_front = edge_at(b, (10, 0, 20))           # the edge along x at y = 0, z = 20
    top_right = edge_at(b, (20, 10, 20))          # the edge along y at x = 20, z = 20
    side = edge_at(b, (20, 0, 10))                # the vertical edge at x = 20, y = 0
    one = fillet(top_front, 2)
    check('fillet of one edge: the edge itself is rounded off (a point at its middle, 0.1 inside both faces, is outside)', value(one, (10, 0.1, 19.9)) > 0, value(one, (10, 0.1, 19.9)))
    check('fillet of one edge: the body is whole beside it (the middle of the top face and of the front face)', value(one, (10, 10, 20)) < 0.05 and value(one, (10, 0, 10)) < 0.05)
    # the edge that runs down from the far end of the rounded one is NOT rounded: its corner stays sharp a little way from the end
    check('fillet of one edge: the edge that runs down from its far end is not rounded (a point 0.1 inside both faces, 6 mm down)', value(one, (19.9, 0.1, 14.0)) < 0, value(one, (19.9, 0.1, 14.0)))
    check('fillet of one edge: the edge along the top at the other side is not rounded', value(one, (19.9, 10, 19.9)) < 0, value(one, (19.9, 10, 19.9)))
    two = fillet(top_front, top_right, 2)
    check('fillet of two edges: both are rounded off', value(two, (10, 0.1, 19.9)) > 0 and value(two, (19.9, 10, 19.9)) > 0, [value(two, (10, 0.1, 19.9)), value(two, (19.9, 10, 19.9))])
    check('fillet of two edges: the third edge is not (the vertical one)', value(two, (19.9, 0.1, 10)) < 0, value(two, (19.9, 0.1, 10)))
    check('fillet of two edges: the faces are whole', value(two, (10, 10, 20)) < 0.05 and value(two, (10, 0, 10)) < 0.05)
    three = chamfer(top_front, top_right, side, 2)
    check('chamfer of three edges: each is cut off', value(three, (10, 0.1, 19.9)) > 0 and value(three, (19.9, 10, 19.9)) > 0 and value(three, (19.9, 0.1, 10)) > 0,
          [value(three, (10, 0.1, 19.9)), value(three, (19.9, 10, 19.9)), value(three, (19.9, 0.1, 10))])
    check('chamfer of three edges: the faces are whole', value(three, (10, 10, 20)) < 0.05 and value(three, (10, 0, 10)) < 0.05)
    check('chamfer of three edges: the edge that is not given is not cut', value(three, (0.1, 10, 19.9)) < 0, value(three, (0.1, 10, 19.9)))


def ends_are_flush():
    # A fillet of a straight edge ends flush in the end faces and touches nothing else (the user, 2026-11-02, picture of a box with a fillet along its top edge: a dome and a stub at each end -- task C18,
    # "a fillet does not spill over neighbouring edges"): the edges that leave the corner at its ends are exactly as they were, a hair from the end
    b = box(20, position=(10, 10, 10))
    top_front = edge_at(b, (10, 0, 20))
    one = fillet(top_front, 2)
    s2 = 2.0 ** 0.5
    mid =(10, (2 * s2 - 2) / s2, 20 - (2 * s2 - 2) / s2)        # (the middle of the arc of the rolling ball: a distance k - r = 0.828 from the edge along the bisector)
    check('fillet of a straight edge: the middle of the arc is on the surface (the ball of radius 2 rolls along the edge)', abs(value(one, mid)) < 0.03, value(one, mid))
    check('fillet of a straight edge: the front face is whole up to where the ball touches it, 2 mm from the edge (0.2 below that, on the face; 0.5 above it, in the air)', abs(value(one, (10, 0, 17.8))) < 0.03 and value(one, (10, 0, 18.5)) > 0.005,
          [value(one, (10, 0, 17.8)), value(one, (10, 0, 18.5))])
    check('fillet of a straight edge: the corner of the edge itself is taken off (0.1 inside both faces)', value(one, (10, 0.1, 19.9)) > 0, value(one, (10, 0.1, 19.9)))
    # (the vertical edges are checked 2.1 below the top, a hair beyond where the ball of the rounding stops, 2 from the edge: the old tube of 2.4 round the edge took them in; the other top edges 3 from the rounded one)
    ends = {'near end, the vertical edge': (0.1, 0.1, 17.9), 'near end, the other top edge': (0.1, 3.0, 19.9), 'far end, the vertical edge': (19.9, 0.1, 17.9), 'far end, the other top edge': (19.9, 3.0, 19.9)}
    for label, p in ends.items():
        check('fillet of a straight edge: %s is not touched (0.1 inside both faces is still -0.1)' % label, abs(value(one, p) + 0.1) < 0.02, value(one, p))
    check('fillet of a straight edge: it goes all the way to the end face (a point 0.01 inside the near end, in the corner, is taken off)', value(one, (0.01, 0.1, 19.9)) > 0, value(one, (0.01, 0.1, 19.9)))
    check('fillet of a straight edge: the end face is whole', abs(value(one, (0.0, 10, 10))) < 0.05)
    live = fillet(top_front, Shape.wrap(2.0) * 1.0)
    check('fillet of a straight edge: the radius may be a field or a var() (the handle of the radius drags it): the same rounding', abs(value(live, mid) - value(one, mid)) < 0.03 and value(live, (10, 0.1, 19.9)) > 0,
          [value(live, mid), value(live, (10, 0.1, 19.9))])
    side = edge_at(b, (20, 0, 10))
    vertical = fillet(side, 2)
    check('fillet of a vertical edge: it is rounded off and its neighbours are not (the top edges at its ends)', value(vertical, (19.9, 0.1, 10)) > 0 and abs(value(vertical, (19.9, 3.0, 19.9)) + 0.1) < 0.02 and abs(value(vertical, (17.0, 0.1, 19.9)) + 0.1) < 0.02,
          [value(vertical, (19.9, 0.1, 10)), value(vertical, (19.9, 3.0, 19.9)), value(vertical, (17.0, 0.1, 19.9))])


def menu():
    others = [{'name': 'edge_2', 'type': 'curve'}, {'name': 'edge_3', 'type': 'curve'}]
    for name in ('fillet', 'chamfer'):
        text = mc._build_operation(name, {'body': 'edge_1', 'body_info': {'type': 'curve'}}, others)
        check('the menu writes every edge that is selected (%s)' % name, text is not None and 'edge_1' in text and 'edge_2' in text and 'edge_3' in text, text)
    alone = mc._build_operation('fillet', {'body': 'box_1', 'body_info': {'type': 'solid'}}, [])
    check('a body alone is still the body (every concave edge)', alone == 'fillet(box_1)', alone)


run('box_edges', box_edges)
run('ends are flush', ends_are_flush)
run('menu', menu)
print('=' * 70)
if bad:
    print(len(bad), 'check(s) failed', bad, '    total %.1f s' % (time.time() - start))
    sys.exit(1)
print('ALL CHECKS PASSED     total %.1f s' % (time.time() - start))
