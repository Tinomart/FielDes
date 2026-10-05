'''
What the viewport's context menus create: the library's primitives and basic operations.

Right-clicking empty space in the viewport offers New primitive (every primitive of the library, placed where the
cursor is) and Add operation; right-clicking a body offers Operation (the same list, with that body passed in).  The
menus are built from the two lists below, and the call each entry writes into the script comes from its template,
so this file is the one place that says what the menus offer.  dev/tests/t_menu_catalog.py runs every template, so an
entry whose function is renamed or whose arguments change fails there rather than in front of the user.

A template is a Python format string.  For a primitive: {x} {y} {z} is the place (the point of the cursor's ray
that is closest to the origin, rounded), {s} the size (about a hundred pixels on screen, a round number), {h} half
of it, {xm} {xp} {ym} {yp} {zm} {zp} the place minus and plus half the size.  For an operation: {body} is the model
it works on, {other} the other models (for the operations that combine models: one name, or several separated by
commas, as when several models are selected -- union, difference, intersection and exclude take any number: for exclude the
first is the shape and the others are the regions locked in it), {s} the size,
{t} a tenth of it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import json
import math

# (function, group, template)
PRIMITIVES = [
    ('sphere', '3D', 'sphere({h}, ({x}, {y}, {z}))'),
    ('box_exact_centered', '3D', 'box_exact_centered(({s}, {s}, {s}), ({x}, {y}, {z}))'),
    ('box_exact', '3D', 'box_exact(({xm}, {ym}, {zm}), ({xp}, {yp}, {zp}))'),
    ('box_centered', '3D', 'box_centered(({s}, {s}, {s}), ({x}, {y}, {z}))'),
    ('box', '3D', 'box(({xm}, {ym}, {zm}), ({xp}, {yp}, {zp}))'),
    ('box_mitered_centered', '3D', 'box_mitered_centered(({s}, {s}, {s}), ({x}, {y}, {z}))'),
    ('box_mitered', '3D', 'box_mitered(({xm}, {ym}, {zm}), ({xp}, {yp}, {zp}))'),
    ('cube_centered', '3D', 'cube_centered(({s}, {s}, {s}), ({x}, {y}, {z}))'),
    ('cube', '3D', 'cube(({xm}, {ym}, {zm}), ({xp}, {yp}, {zp}))'),
    ('rounded_box', '3D', 'rounded_box(({xm}, {ym}, {zm}), ({xp}, {yp}, {zp}), 0.3)'),
    ('rounded_cube', '3D', 'rounded_cube(({xm}, {ym}, {zm}), ({xp}, {yp}, {zp}), 0.3)'),
    ('cylinder_z', '3D', 'cylinder_z({h}, {s}, ({x}, {y}, {zm}))'),
    ('cylinder', '3D', 'cylinder({h}, {s}, ({x}, {y}, {zm}))'),
    ('cone_z', '3D', 'cone_z({h}, {s}, ({x}, {y}, {zm}))'),
    ('cone', '3D', 'cone({h}, {s}, ({x}, {y}, {zm}))'),
    ('cone_ang_z', '3D', 'cone_ang_z(0.5, {s}, ({x}, {y}, {zm}))'),
    ('cone_ang', '3D', 'cone_ang(0.5, {s}, ({x}, {y}, {zm}))'),
    ('torus_z', '3D', 'torus_z({h}, {q}, ({x}, {y}, {z}))'),
    ('torus', '3D', 'torus({h}, {q}, ({x}, {y}, {z}))'),
    ('pyramid_z', '3D', 'pyramid_z(({xm}, {ym}), ({xp}, {yp}), {zm}, {s})'),
    ('half_space', '3D', 'half_space((0, 0, 1), ({x}, {y}, {z}))'),
    ('gyroid', '3D', 'gyroid(({s}, {s}, {s}), {t})'),
    ('circle', '2D', 'circle({h}, ({x}, {y}))'),
    ('rectangle', '2D', 'rectangle(({xm}, {ym}), ({xp}, {yp}))'),
    ('rectangle_exact', '2D', 'rectangle_exact(({xm}, {ym}), ({xp}, {yp}))'),
    ('rectangle_centered_exact', '2D', 'rectangle_centered_exact(({s}, {s}), ({x}, {y}))'),
    ('rounded_rectangle', '2D', 'rounded_rectangle(({xm}, {ym}), ({xp}, {yp}), 0.3)'),
    ('ring', '2D', 'ring({h}, {q}, ({x}, {y}))'),
    ('polygon', '2D', 'polygon({h}, 6, ({x}, {y}))'),
    ('triangle', '2D', 'triangle(({xm}, {ym}), ({xp}, {ym}), ({x}, {yp}))'),
]

# (function, group, template, needs a second model)
OPERATIONS = [
    ('offset', 'Offsets and walls', 'offset({body}, {t})', False),
    ('shell', 'Offsets and walls', 'shell({body}, {t})', False),
    ('thicken', 'Offsets and walls', 'thicken({body}, {t})', False),
    ('shell_inside', 'Offsets and walls', 'shell_inside({body}, {t})', False),
    ('shell_outside', 'Offsets and walls', 'shell_outside({body}, {t})', False),
    ('shell_centered', 'Offsets and walls', 'shell_centered({body}, {t})', False),
    ('offset_exact', 'Offsets and walls', 'offset_exact({body}, {t})', False),
    ('shell_exact', 'Offsets and walls', 'shell_exact({body}, {t})', False),
    ('smooth', 'Rounding', 'smooth({body}, {t}, 3)', False),
    ('round_edges', 'Rounding', 'round_edges({body}, {t})', False),
    ('fillet', 'Rounding', 'fillet({body}, {t})', False),
    ('move', 'Moving', 'move({body}, ({s}, 0, 0))', False),
    ('rotate_z', 'Moving', 'rotate_z({body}, 0.7854)', False),
    ('scale_xyz', 'Moving', 'scale_xyz({body}, (1.5, 1.5, 1.5))', False),
    ('reflect_x', 'Moving', 'reflect_x({body})', False),
    ('symmetric_x', 'Moving', 'symmetric_x({body})', False),
    ('symmetric_y', 'Moving', 'symmetric_y({body})', False),
    ('symmetric_z', 'Moving', 'symmetric_z({body})', False),
    ('array_x', 'Repeating', 'array_x({body}, 3, {s})', False),
    ('array_xy', 'Repeating', 'array_xy({body}, 3, 3, ({s}, {s}))', False),
    ('array_polar_z', 'Repeating', 'array_polar_z({body}, 6)', False),
    ('union', 'Combining', 'union({body}, {other})', True),
    ('difference', 'Combining', 'difference({body}, {other})', True),
    ('intersection', 'Combining', 'intersection({body}, {other})', True),
    ('exclude', 'Combining', 'exclude({body}, {other})', True),
]


def catalog():
    ''' The two lists as JSON for the menus: {"primitives": [{name, group}], "operations": [{name, group, other}]} '''
    return json.dumps({
        'primitives': [{'name': n, 'group': g} for n, g, _ in PRIMITIVES],
        'operations': [{'name': n, 'group': g, 'other': o} for n, g, _, o in OPERATIONS],
    })


def _num(v):
    ''' A number as it is written in a script: at most four decimals, no trailing zeros '''
    s = ('%.4f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('', '-0') else s


def _nice(v):
    ''' The round number (1, 2 or 5 times a power of ten) nearest to v on a logarithmic scale '''
    if not v > 0:
        return 10.0
    e = math.floor(math.log10(v))
    f = v / 10 ** e
    best = min((1.0, 2.0, 5.0, 10.0), key=lambda c: abs(math.log(f / c)))
    return best * 10 ** e


def _step(s):
    ''' The grid a place snaps to: the largest 1, 2 or 5 times a power of ten that is at most a twentieth of the size '''
    target = s / 20.0
    e = math.floor(math.log10(target))
    for c in (5.0, 2.0, 1.0):
        if c * 10 ** e <= target * 1.0000001:
            return c * 10 ** e
    return 10 ** e


def call(request):
    ''' The text of the call a menu entry writes.  `request` is JSON: {kind: 'primitive' | 'operation', name,
        x, y, z (the place), scale (mm spanned by about a hundred pixels there), body, other (names or
        expressions of the models) } '''
    r = json.loads(request) if isinstance(request, str) else request
    kind, name = r['kind'], r['name']
    s = _nice(float(r.get('scale', 10.0)))
    step = _step(s)

    def snap(v):
        return round(float(v) / step) * step

    x, y, z = snap(r.get('x', 0.0)), snap(r.get('y', 0.0)), snap(r.get('z', 0.0))
    h = s / 2.0
    values = {
        'x': _num(x), 'y': _num(y), 'z': _num(z), 's': _num(s), 'h': _num(h), 't': _num(s / 10.0), 'q': _num(s / 6.0),
        'xm': _num(x - h), 'xp': _num(x + h), 'ym': _num(y - h), 'yp': _num(y + h), 'zm': _num(z - h), 'zp': _num(z + h),
        'body': r.get('body', ''), 'other': r.get('other', ''),
    }
    if kind == 'primitive':
        for n, _, template in PRIMITIVES:
            if n == name:
                return template.format(**values)
    elif kind == 'operation':
        for n, _, template, _ in OPERATIONS:
            if n == name:
                if not values['body']:
                    raise ValueError('%s needs a model to work on' % name)
                if '{other}' in template and not values['other']:
                    raise ValueError('%s needs a second model' % name)
                return template.format(**values)
    raise ValueError('the menu has no %s called %s' % (kind, name))
