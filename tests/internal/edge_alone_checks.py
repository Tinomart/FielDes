import json, math, sys, traceback
from fieldes import *
from fieldes import menu_catalog as mc, completion

bad = []
def check(label, ok, extra=''):
    print(('ok   ' if ok else 'FAIL ') + label + ((' [' + extra + ']') if extra and not ok else ''))
    if not ok:
        bad.append(label)

def inside(s, p):
    return evaluate(s, [p])[0] < 0

def menu(name, **kw):
    r = dict(kind='operation', name=name, x=0, y=0, z=0, scale=10, indent=0, var=name + '_1')
    r.update(kw)
    return mc.call(json.dumps(r))

# ---- what the menu writes
for name in ('fillet', 'chamfer'):
    one = ' '.join(menu(name, body='edge_1', body_info={'type': 'curve'}, others=[]).split())
    check(name + ': the edge alone -> ' + one, one == '%s( edge_1, %s=..., )' % (name, 'radius' if name == 'fillet' else 'size'), one)
    two = ' '.join(menu(name, body='box_1', body_info={'type': 'solid'}, others=[{'name': 'edge_1', 'type': 'curve'}]).split())
    check(name + ': body then edge -> the edge alone', two == one, two)
    three = ' '.join(menu(name, body='edge_1', body_info={'type': 'curve'}, others=[{'name': 'box_1', 'type': 'solid'}]).split())
    check(name + ': edge then body -> the edge alone', three == one, three)
body = ' '.join(menu('fillet', body='box_1', body_info={'type': 'solid'}, others=[]).split())
check('fillet: a body alone -> fillet(box_1, radius=...) -> ' + body, body == 'fillet( box_1, radius=..., )', body)
fld = ' '.join(menu('fillet', body='edge_1', body_info={'type': 'curve'}, others=[{'name': 'f_1', 'type': 'field'}]).split())
check('fillet: an edge and a field -> the field is the radius -> ' + fld, fld == 'fillet( edge_1, radius=f_1, )', fld)
try:
    menu('chamfer', body='box_1', body_info={'type': 'solid'}, others=[])
    check('chamfer: a body alone is not offered', False)
except ValueError:
    check('chamfer: a body alone is not offered', True)

# ---- several edges: written like one, the placeholder for the number after the last of them
many = [{'name': 'edge_2', 'type': 'curve'}, {'name': 'edge_3', 'type': 'curve'}]
for name in ('fillet', 'chamfer'):
    slot = 'radius' if name == 'fillet' else 'size'
    text = ' '.join(menu(name, body='edge_1', body_info={'type': 'curve'}, others=many).split())
    check(name + ': three edges -> ' + text, text == '%s( edge_1, edge_2, edge_3, %s=..., )' % (name, slot), text)
    text = ' '.join(menu(name, body='box_1', body_info={'type': 'solid'}, others=[{'name': 'edge_1', 'type': 'curve'}] + many[:1]).split())
    check(name + ': a body and two edges -> the edges alone -> ' + text, text == '%s( edge_1, edge_2, %s=..., )' % (name, slot), text)
text = ' '.join(menu('fillet', body='edge_1', body_info={'type': 'curve'}, others=many + [{'name': 'f_1', 'type': 'field'}]).split())
check('fillet: three edges and a field -> the field is the radius -> ' + text, text == 'fillet( edge_1, edge_2, edge_3, radius=f_1, )', text)

# ---- what is written runs
b = box(10, position=(5, 5, 5))
e = edge_at(b, (10, 10, 5))
check('edge knows its body', getattr(e, '_on', None) is b)
f = fillet(e, 2)
check('fillet(edge, 2): the edge is rounded off', not inside(f, (9.9, 9.9, 5)) and inside(f, (5, 5, 5)) and inside(f, (9.9, 0.1, 5)))
f2 = fillet(e, radius=2)
check('fillet(edge, radius=2) the same', not inside(f2, (9.9, 9.9, 5)))
f3 = fillet(b, e, 2)
check('fillet(body, edge, 2) still works', not inside(f3, (9.9, 9.9, 5)))
e2 = edge_at(b, (5, 10, 10))
m2 = fillet(e, e2, radius=2)
check('fillet(edge, edge, radius=2): both edges are rounded off', not inside(m2, (9.9, 9.9, 5)) and not inside(m2, (5, 9.9, 9.9)) and inside(m2, (5, 5, 5)))
m3 = fillet(e, e2, 2)
check('fillet(edge, edge, 2): a number among the edges is the radius, told by what it is', not inside(m3, (9.9, 9.9, 5)) and not inside(m3, (5, 9.9, 9.9)))
m4 = chamfer(e, e2, size=2)
check('chamfer(edge, edge, size=2): both edges are cut', not inside(m4, (9.2, 9.2, 5)) and not inside(m4, (5, 9.2, 9.2)))
try:
    fillet(e, e2)
    check('fillet(edge, edge) without a radius is an error', False)
except ValueError as ex:
    check('fillet(edge, edge) without a radius is an error: ' + str(ex)[:70], 'radius' in str(ex))
f4 = fillet(e, b, 2)
check('fillet(edge, body, 2) in any order', not inside(f4, (9.9, 9.9, 5)))
c = chamfer(e, 2)
check('chamfer(edge, 2): cut off', not inside(c, (9.2, 9.2, 5)) and inside(c, (8.8, 8.8, 5)) and inside(c, (9.9, 0.1, 5)))
c2 = chamfer(b, e, 2)
check('chamfer(body, edge, 2) still works', not inside(c2, (9.2, 9.2, 5)))
ell = union(box((10, 10, 4), position=(5, 5, 2)), box((4, 10, 10), position=(2, 5, 5)))
all_ = fillet(ell, 1)
check('fillet(body, radius) still fills every concave edge', not inside(ell, (4.2, 5, 4.2)) and inside(all_, (4.2, 5, 4.2)))
for call, label in ((lambda: fillet(e), 'fillet(edge) without a radius'), (lambda: chamfer(e), 'chamfer(edge) without a size'),
                    (lambda: fillet(b, e), 'fillet(body, edge) without a radius'), (lambda: fillet(e, 1, 2), 'fillet(edge, 1, 2)')):
    try:
        call()
        check(label + ' is an error', False)
    except (TypeError, ValueError) as ex:
        check(label + ' is an error: ' + str(ex)[:90], True)

# ---- the completion
text = completion.complete_call('fillet(edge_1)', 0, 0)
check('completion of fillet(edge_1): ' + repr(text), ' '.join(text.split()) == 'fillet( edge_1, radius=..., )', text)
text = completion.complete_call('chamfer(edge_1, size=0.5)', 0, 0)
check('completion of chamfer(edge_1, size=0.5): ' + repr(text), 'edge_1' in text and '0.5' in text and '...' not in text, text)
text = completion.complete_call('fillet(edge_3, edge_2, edge_1)', 0, 0)
check('completion of fillet(edge_3, edge_2, edge_1): ' + repr(text), ' '.join(text.split()) == 'fillet( edge_3, edge_2, edge_1, radius=..., )', text)
text = completion.complete_call('chamfer(edge_3, edge_2, edge_1)', 0, 0)
check('completion of chamfer(edge_3, edge_2, edge_1): ' + repr(text), ' '.join(text.split()) == 'chamfer( edge_3, edge_2, edge_1, size=..., )', text)
text = completion.complete_call('fillet()', 0, 0)
check('completion of fillet(): ' + repr(text), ' '.join(text.split()) == 'fillet( edge=..., radius=..., )', text)
print('FAILED:' if bad else 'ALL PASSED', bad)
