'''
What the viewport's context menus create: the library's primitives and basic operations.

Right-clicking empty space in the viewport offers New 3D shape / 2D shape / point / surface / field (every primitive of the library, by kind, placed where the
cursor is), New custom block and Add operation; right-clicking a body offers Operation (the same list, with that body passed in).  The
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
    ('point', 'Points', 'point({x}, {y}, {z})'),
    ('plane', 'Surfaces', 'plane(({x}, {y}, {z}), (0, 0, 1))'),
    ('sphere_surface', 'Surfaces', 'sphere_surface({h}, ({x}, {y}, {z}))'),
    ('cylinder_surface', 'Surfaces', "cylinder_surface({h}, 'z', ({x}, {y}, 0))"),
    ('wave_surface', 'Surfaces', 'wave_surface({q}, {s}, "x", {z})'),
    ('distance_to_point', 'Fields', 'distance_to_point(({x}, {y}, {z}))'),
    ('radial_field', 'Fields', "radial_field(({x}, {y}, {z}), 'z')"),
    ('z_field', 'Fields', 'z_field()'),
    ('noise_field', 'Fields', 'noise_field({s}, 3)'),
    ('wave', 'Fields', "wave('x', {s}, {t})"),
]

# What a group of primitives makes (the kind: see fieldes.kinds), for the icon beside each entry
GROUP_KINDS = {'3D': 'solid', '2D': 'profile', 'Points': 'point', 'Surfaces': 'surface', 'Fields': 'field'}

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
    # Arithmetic on fields (a number is a field too; of a body they work on its values, which makes a field)
    ('add_fields', 'Field math', 'add_fields({body}, {other})', True),
    ('subtract_fields', 'Field math', 'subtract_fields({body}, {other})', True),
    ('multiply_fields', 'Field math', 'multiply_fields({body}, {other})', True),
    ('divide_fields', 'Field math', 'divide_fields({body}, {other})', True),
    ('power_field', 'Field math', 'power_field({body}, {other})', True),
    ('min_fields', 'Field math', 'min_fields({body}, {other})', True),
    ('max_fields', 'Field math', 'max_fields({body}, {other})', True),
    ('abs_field', 'Field math', 'abs_field({body})', False),
    ('negate_field', 'Field math', 'negate_field({body})', False),
    ('sqrt_field', 'Field math', 'sqrt_field({body})', False),
    ('square_field', 'Field math', 'square_field({body})', False),
    ('field_from_body', 'Field math', 'field_from_body({body})', False),
    # Simulations: the model is the part (the fluid, for the flow); the supports and loads are boxes laid on its lower and upper
    # ends (its left and right ends for the flow), a first problem to change in the script
    ('static_analysis', 'Simulations',
     'static_analysis({body}, static_boundary_conditions({body}, supports=[fixed({bottom})], '
     'loads=[force({top}, (0, 0, -100))]), element_size={es})', False),
    ('modal_analysis', 'Simulations',
     'modal_analysis({body}, static_boundary_conditions({body}, supports=[fixed({bottom})]), modes=6, element_size={es})', False),
    ('topology_optimization', 'Simulations',
     'topology_optimization({body}, static_boundary_conditions({body}, supports=[fixed({bottom})], '
     'loads=[force({top}, (0, 0, -100))]), volume_fraction=0.3, element_size={es})', False),
    ('thermal_analysis', 'Simulations',
     'thermal_analysis({body}, [fixed_temperature({bottom}, 20), heat_input({top}, 5.0)], element_size={es})', False),
    ('fluid_analysis', 'Simulations',
     'fluid_analysis({body}, [inlet({left}, speed=100), outlet({right})], fluid=water, element_size={es})', False),
]


def _blocks():
    ''' The custom blocks the menus offer (see fieldes.blocks): [{name, doc, operation, primitive}] '''
    try:
        from fieldes import blocks
        return [b for b in blocks.info() if b['operation'] or b['primitive']]
    except Exception:
        return []


def catalog():
    ''' The lists as JSON for the menus: {"primitives": [{name, group, type}], "operations": [{name, group, other}]}; the custom
        blocks are in them too (group "Custom blocks", type "block") '''
    from fieldes.kinds import describe
    blocks = _blocks()
    return json.dumps({
        'kinds': describe(),
        'primitives': [{'name': n, 'group': g, 'type': GROUP_KINDS.get(g, 'solid')} for n, g, _ in PRIMITIVES]
                      + [{'name': b['name'], 'group': 'Custom blocks', 'type': 'block', 'doc': b['doc']}
                         for b in blocks if b['primitive']],
        'operations': [{'name': n, 'group': g, 'other': o} for n, g, _, o in OPERATIONS]
                      + [{'name': b['name'], 'group': 'Custom blocks', 'other': False, 'type': 'block', 'doc': b['doc']}
                         for b in blocks if b['operation']],
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


def _regions(r, scale):
    ''' What a simulation's call is written with, from the bounds of the model it works on (`lo`, `hi` of the request; the
        place and size of the click when there are none): the boxes on its lower end (`bottom`), upper end (`top`), left end
        (`left`) and right end (`right`), each a tenth of its length thick and a little past its sides, and `es`, the
        element size (a twentieth of its longest side) '''
    lo, hi = r.get('lo'), r.get('hi')
    ok = (isinstance(lo, (list, tuple)) and isinstance(hi, (list, tuple)) and len(lo) == 3 and len(hi) == 3 and
          all(math.isfinite(float(c)) and abs(float(c)) < 1e6 for c in list(lo) + list(hi)) and
          all(float(hi[i]) > float(lo[i]) for i in range(3)))
    if ok:
        lo, hi = [float(c) for c in lo], [float(c) for c in hi]
    else:
        c = [float(r.get(k, 0.0)) for k in ('x', 'y', 'z')]
        lo, hi = [v - scale for v in c], [v + scale for v in c]
    longest = max(hi[i] - lo[i] for i in range(3))
    margin = 0.01 * longest

    def box(axis, side):
        a, b = list(lo), list(hi)
        for i in range(3):
            a[i] -= margin
            b[i] += margin
        length = hi[axis] - lo[axis]
        if side == 0:
            b[axis] = lo[axis] + 0.1 * length
        else:
            a[axis] = hi[axis] - 0.1 * length
        return 'box_exact(({}, {}, {}), ({}, {}, {}))'.format(*[_num(v) for v in a + b])

    return {'bottom': box(2, 0), 'top': box(2, 1), 'left': box(0, 0), 'right': box(0, 1),
            'es': _num(_nice(longest / 20.0))}


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
    values.update(_regions(r, s))
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
    for b in _blocks():                 # (a custom block: its call, with its other arguments left to their defaults)
        if b['name'] == name and kind == 'primitive' and b['primitive']:
            return name + '()'
        if b['name'] == name and kind == 'operation' and b['operation']:
            if not values['body']:
                raise ValueError('%s needs a model to work on' % name)
            return '%s(%s)' % (name, values['body'])
    raise ValueError('the menu has no %s called %s' % (kind, name))
