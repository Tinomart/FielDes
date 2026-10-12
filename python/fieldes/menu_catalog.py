'''
What the viewport's context menus create: the library's primitives and basic operations.

Right-clicking empty space in the viewport offers New 3D shape / 2D shape / point / surface / field (every primitive of the library, by kind, placed where the
cursor is), New custom block and Add operation; right-clicking a body offers Operation (the same list, with that body passed in).  The
menus are built from the two lists below, and the call each entry writes into the script comes from its template,
so this file is the one place that says what the menus offer.

A template is a Python format string.  For a primitive: {x} {y} {z} is the place (the point of the cursor's ray
that is closest to the origin, rounded), {s} the size (about a hundred pixels on screen, a round number), {h} half
of it, {xm} {xp} {ym} {yp} {zm} {zp} the place minus and plus half the size.  For an operation: {body} is the model
it works on, {other} the other models (for the operations that combine models: one name, or several separated by
commas, as when several models are selected -- union, difference, intersection and exclude take any number: for exclude the
first is the shape and the others are the regions locked in it), {others} the same with a comma before it, or nothing when there
are none (surface_from_bodies: the surface of one model, or where the others meet it), {s} the size, {t} a tenth of it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import inspect
import json
import math

from fieldes.i18n import tr

# What a template may put ABOVE the line it writes (a function the call uses, for the user to write their own logic in): the
# text before this line is written above the assignment, the text after it is the call.  {var} is the variable the model gets.
PRELUDE = '\n#@@#\n'

_LOW_LEVEL_FIELD = '''def {var}_logic(x, y, z):
    # Your own field: x, y and z are the coordinates (fields). Return the value you want at that point.
    # Write the math with + - * / **, x.sqrt() x.square() x.abs() x.sin() x.cos(), a.max(b) a.min(b); a number is a constant field.
    return ((x - {x}).square() + (y - {y}).square() + (z - {z}).square()).sqrt() - {h}     # (here: the distance from a point, less {h})
#@@#
low_level_field({var}_logic)'''

_LOW_LEVEL_BODY = '''def {var}_logic(x, y, z):
    # Your own body: x, y and z are the coordinates (fields). Return a number that is negative inside the body, zero on its
    # surface and positive outside -- ideally the distance to the surface. Math: + - * / **, x.sqrt() x.square() x.abs() x.sin()
    # x.cos(), a.max(b) a.min(b), maximum(a, b, c) minimum(a, b, c).
    return ((x - {x}).square() + (y - {y}).square() + (z - {z}).square()).sqrt() - {h}     # (here: a ball of radius {h})
#@@#
low_level_body({var}_logic)'''

# A condition made with no model to say where it acts has a PLACEHOLDER there (`...`: the run stops at it until a body, a field or a
# surface is dropped on it in the model tree, or written in its place) -- the menu invents no region of its own
def _g(value):
    return '%.6g' % value


def _materials():
    ''' The materials the menus offer: the presets of the library, written out with their numbers (so that they are there to
        change), a Material of your own, and the fluids '''
    out = []
    try:
        from fieldes.stdlib import fea, fluid
        for name in ('steel', 'stainless_steel', 'aluminium', 'titanium', 'pla', 'petg', 'abs_plastic', 'nylon'):
            m = getattr(fea, name)
            out.append((name, 'Materials', 'Material(%r, %s, %s, %s, %s, %s, %s)' % (
                m.name, _g(m.E), _g(m.nu), _g(m.density), _g(m.yield_strength), _g(m.conductivity), _g(m.expansion))))
        out.append(('material', 'Materials', "Material('{mname}', {mE}, {mnu}, {mdens}, {myield})"))
        for name in ('water', 'air', 'oil', 'glycerol'):
            f = getattr(fluid, name)
            out.append((name, 'Fluids', 'Fluid(%r, %s, %s)' % (f.name, _g(f.density), _g(f.viscosity))))
    except Exception:
        pass
    return out


def _cell_entries():
    ''' The cells of the library as menu entries (name, group, template, label), made from what the library itself lists: every strut cell,
        every TPMS, every planar pattern, and the three custom cells.  Each writes the
        call that makes the cell (`cell_periodic('gyroid')`); the name of the entry is `cell_<kind>`, its label is the kind '''
    out = []
    try:
        from fieldes.stdlib import lattices as L
    except Exception:
        return out

    def listed(table, group, alias=None):
        seen = set()
        for key, fn in table.items():
            if id(fn) in seen:
                continue                            # (the other names of the same cell: simple_cubic, schwarz_d ...)
            seen.add(id(fn))
            kind = alias.get(key, key) if alias else key
            out.append(('cell_' + kind, group, "cell_periodic('%s')" % kind, kind))

    listed(L.STRUT_CELLS, 'Strut cells', {'diamond': 'diamond_struts'})
    listed(L.TPMS_KINDS, 'TPMS cells')
    for kind in L.PLANAR_KINDS:
        out.append(('cell_' + kind, 'Planar cells', "cell_periodic('%s')" % kind, kind))
    out.append(('cell_custom', 'Custom cells', 'cell_custom(box((0, 0, 0), ({s}, {s}, {s})), sphere({h}, ({h}, {h}, {h})))',
                'custom'))
    out.append(('cell_custom_truss', 'Custom cells',
                "cell_custom_truss({{'c': (0.5, 0.5, 0.5), 'o': (0, 0, 0)}}, [('c', 'o')], mirror='xyz')", 'custom_truss'))
    out.append(('cell_custom_tpms', 'Custom cells',
                'cell_custom_tpms(lambda a, b, c: a.sin() * b.cos() + b.sin() * c.cos() + c.sin() * a.cos())', 'custom_tpms'))
    return out


_CELLS = _cell_entries()
# What an entry is called in the menu, where that is not its name (the cells: `cell_gyroid` is the gyroid)
LABELS = {name: label for name, _g, _t, label in _CELLS}
# The sections of the menus that a group of entries belongs to, besides the first menu: the lattice section holds the lattice operations
# (and the points a foam is made around) and every cell
SECTIONS = {'Strut cells': 'lattice', 'TPMS cells': 'lattice', 'Planar cells': 'lattice', 'Custom cells': 'lattice', 'Lattices': 'lattice'}

# What the Lattice menu says of an entry when the mouse rests on it
HINTS = {
    'lattice': tr('With a cell selected with the model: that cell.  Otherwise a first cell to change in the script.'),
    'lattice_surface_conform': tr('With a cell selected with the model: that cell.  Otherwise a first cell to change in the script.'),
    'random_points': tr("Points made in the model, drawn as dots: how far apart they are on average, how random, the nearest and the farthest two "
                        "may be.  A foam is made around them: select them and choose voronoi_lattice or delaunay_lattice"),
    'voronoi_lattice': tr('A foam: the edges of the Voronoi cells around the selected points, as beams, filling the model the points were made in.  '
                          'Select points first (random_points)'),
    'delaunay_lattice': tr('A stochastic truss: beams along the Delaunay edges between the selected points, filling the model the points were '
                           'made in.  Select points first (random_points)'),
}

# A sketch or a curve made from the menu: its points are made with it (each a model of its own, drawn as a dot, with the gizmo that moves and
# rotates it), in the horizontal plane at the place of the click
_SKETCH = '''{var}_p1 = point({xm}, {ym}, {z})
{var}_p1
{var}_p2 = point({xp}, {ym}, {z})
{var}_p2
{var}_p3 = point({xp}, {yp}, {z})
{var}_p3
{var}_p4 = point({xm}, {yp}, {z})
{var}_p4
#@@#
sketch([{var}_p1, {var}_p2, {var}_p3, {var}_p4])'''

_CURVE = '''{var}_p1 = point({xm}, {y}, {z})
{var}_p1
{var}_p2 = point({x}, {yp}, {z})
{var}_p2
{var}_p3 = point({xp}, {y}, {z})
{var}_p3
#@@#
curve([{var}_p1, {var}_p2, {var}_p3])'''

_SWEEP = '''{var}_p1 = point({xm}, {y}, {z})
{var}_p1
{var}_p2 = point({x}, {yp}, {z})
{var}_p2
{var}_p3 = point({xp}, {y}, {z})
{var}_p3
{var}_path = curve([{var}_p1, {var}_p2, {var}_p3])
{var}_path
#@@#
path_extrude(circle({q}), {var}_path)'''

# What the entries that are made of the models selected say when the mouse rests on them (the shape the model tree has: points, a curve, a 2D shape)
FROM_SELECTION = {
    'sketch': tr('A 2D shape from points.  With points selected: made from them, in the order you selected them.  With none: four new points to drag.  '
                 'Click its edge to put a point there; double-click a point to make it a corner or a spline point'),
    'curve': tr('A line through points.  With points selected: made from them, in the order you selected them.  With none: three new points to drag.  '
                'Click the line and drag to put a point there; double-click a point to make it a corner or a spline point'),
    'extrude': tr('A 3D shape from a 2D shape: with a 2D shape selected it is pulled up; with none, a new one is made'),
    'path_extrude': tr('A 2D shape swept along a curve: select both -- or the shape and the points of the path.  With none selected, a new circle and a path are made'),
}

# (function, group, template)
PRIMITIVES = [
    ('sphere', '3D', 'sphere({h}, ({x}, {y}, {z}))'),
    ('box', '3D', 'box(({xm}, {ym}, {zm}), ({xp}, {yp}, {zp}))'),
    ('rounded_box', '3D', 'rounded_box(({xm}, {ym}, {zm}), ({xp}, {yp}, {zp}), {r})'),
    ('cylinder', '3D', 'cylinder({h}, {s}, ({x}, {y}, {zm}))'),
    ('cone', '3D', 'cone({h}, {s}, ({x}, {y}, {zm}))'),
    ('torus', '3D', 'torus({h}, {q}, ({x}, {y}, {z}))'),
    ('pyramid', '3D', 'pyramid(({xm}, {ym}), ({xp}, {yp}), {zm}, {s})'),
    ('half_space', '3D', 'half_space((0, 0, 1), ({x}, {y}, {z}))'),
    ('low_level_body', '3D', _LOW_LEVEL_BODY),
    ('circle', '2D', 'circle({h}, ({x}, {y}))'),
    ('rectangle', '2D', 'rectangle(({xm}, {ym}), ({xp}, {yp}))'),
    ('rounded_rectangle', '2D', 'rounded_rectangle(({xm}, {ym}), ({xp}, {yp}), {r})'),
    ('ring', '2D', 'ring({h}, {q}, ({x}, {y}))'),
    ('polygon', '2D', 'polygon({h}, {n}, ({x}, {y}))'),
    ('triangle', '2D', 'triangle(({xm}, {ym}), ({xp}, {ym}), ({x}, {yp}))'),
    ('sketch', '2D', _SKETCH),
    ('curve', '1D', _CURVE),
    ('extrude', '3D', 'extrude(rectangle(({xm}, {ym}), ({xp}, {yp})), 0, {s})'),
    ('path_extrude', '3D', _SWEEP),
    ('point', 'Points', 'point({x}, {y}, {z})'),
    ('plane', 'Surfaces', 'plane(({x}, {y}, {z}), (0, 0, 1))'),
    ('sphere_surface', 'Surfaces', 'sphere_surface({h}, ({x}, {y}, {z}))'),
    ('cylinder_surface', 'Surfaces', "cylinder_surface({h}, 'z', ({x}, {y}, 0))"),
    ('wave_surface', 'Surfaces', 'wave_surface({q}, {s}, "x", {z})'),
    ('distance_to_point', 'Fields', 'distance_to_point(({x}, {y}, {z}))'),
    ('radial_field', 'Fields', "radial_field(({x}, {y}, {z}), 'z')"),
    ('z_field', 'Fields', 'z_field()'),
    ('noise_field', 'Fields', 'noise_field({s}, {n})'),
    ('wave', 'Fields', "wave('x', {s}, {t})"),
    ('low_level_field', 'Fields', _LOW_LEVEL_FIELD),
    # What an analysis is given
    ('fixed', 'Supports and loads', 'fixed()'),
    ('force', 'Supports and loads', 'force(vector=(0, -100, 0))'),
    ('gravity', 'Supports and loads', 'gravity()'),
    ('thermal_expansion', 'Supports and loads', 'thermal_expansion({temp})'),
    ('fixed_temperature', 'Thermal conditions', 'fixed_temperature(value={temp})'),
    ('heat_input', 'Thermal conditions', 'heat_input(watts={watts})'),
    ('heat_generation', 'Thermal conditions', 'heat_generation(watts={watts})'),
    ('convection', 'Thermal conditions', 'convection(coefficient={h}, ambient={ambient})'),
    ('inlet', 'Flow conditions', 'inlet(speed={speed})'),
    ('outlet', 'Flow conditions', 'outlet()'),
    ('wall', 'Flow conditions', 'wall()'),
    ('slip', 'Flow conditions', 'slip()'),
] + _materials() + [(n, g, t) for n, g, t, _l in _CELLS]

# The number of an operation's call that a FIELD takes the place of when one is selected with the body -- `offset(plate, swell)`, `lattice(part, cell, thickness=density)`
# -- the way a field dropped on the call in the model tree does it (every size, radius, thickness of the library takes a field)
FIELD_SLOTS = {n: 't' for n in ('offset', 'shell', 'thicken', 'shell_inside', 'shell_outside', 'shell_centered', 'offset_exact',
                                'shell_exact', 'smooth', 'round_edges', 'fillet')}
FIELD_SLOTS['lattice'] = 'ct'

# The values the templates of an entry are filled with where the menu has nothing better than a default: a number, a text, or '$key' (what the menu
# works out for the place and the size of the model).  Nothing is asked: what an entry writes is in the script, in front of the user, to be edited
# there -- every argument of the call, the defaults too (see fieldes.completion)
PARAMS = {
    'offset': {'t': '$t'},
    'offset_exact': {'t': '$t'},
    'thicken': {'t': '$t'},
    'shell': {'t': '$t'},
    'shell_inside': {'t': '$t'},
    'shell_outside': {'t': '$t'},
    'shell_centered': {'t': '$t'},
    'shell_exact': {'t': '$t'},
    'smooth': {'t': '$t', 'n': 3},
    'round_edges': {'t': '$t'},
    'fillet': {'t': '$t'},
    'move': {'dx': '$s', 'dy': 0, 'dz': 0},
    'rotate_z': {'angle': 45},
    'scale_xyz': {'sx': 1.5, 'sy': 1.5, 'sz': 1.5},
    'array_x': {'n': 3, 's': '$s'},
    'array_xy': {'nx': 3, 'ny': 3, 'dx': '$s', 'dy': '$s'},
    'array_polar_z': {'n': 6},
    'lattice': {'cell': 'gyroid', 'cs': '$cs', 'ct': '$ct'},
    'lattice_surface_conform': {'cell': 'octet', 'cs': '$cs'},
    'rounded_box': {'r': 0.3},
    'rounded_rectangle': {'r': 0.3},
    'polygon': {'n': 6},
    'noise_field': {'n': 3},
    'static_analysis': {'es': '$es'},
    'modal_analysis': {'modes': 6, 'es': '$es'},
    'topology_optimization': {'vf': 0.3, 'es': '$es'},
    'thermal_analysis': {'temp': 20, 'watts': 5, 'es': '$es'},
    'fluid_analysis': {'speed': 100, 'es': '$es'},
    'fixed_temperature': {'temp': 20},
    'heat_input': {'watts': 5},
    'heat_generation': {'watts': 5},
    'convection': {'h': 2.5e-05, 'ambient': 20},
    'inlet': {'speed': 100},
    'thermal_expansion': {'temp': 60},
    'material': {'mname': 'my material', 'mE': 70000, 'mnu': 0.33, 'mdens': 2.7e-09, 'myield': 100},
}


def _fmt(v):
    ''' A number as it is written in a script: whole numbers without decimals, the others in their shortest form '''
    v = float(v)
    return str(int(v)) if v == int(v) and abs(v) < 1e12 else ('%.8g' % v)


def _default_text(d, values):
    if isinstance(d, str):
        return values[d[1:]] if d.startswith('$') else d
    return _fmt(d)


def _apply_params(name, values):
    ''' The values of the entry that the menu fills in itself: its defaults '''
    for k, d in PARAMS.get(name, {}).items():
        values[k] = _default_text(d, values)
    angle = values.get('angle')
    if angle is not None:
        values['angle_rad'] = _fmt(round(math.radians(float(angle)), 6))
    return values


# The conditions of a simulation (primitives of the menu) written for a BODY and the REGIONS it acts on when several models are selected: the
# first is the body, the others where the condition is.  {region} is the body's surface there (surface_from_bodies; a selected
# surface stands for itself), {volume} the regions themselves (a heat generated in a volume).  With one model selected -- or none -- the
# condition has a placeholder where it acts (a body, a field or a surface is dropped on it)
BODY_CONDITIONS = {
    'fixed': 'fixed({region})',
    'force': 'force({region}, vector=(0, -100, 0))',
    'fixed_temperature': 'fixed_temperature({region}, value={temp})',
    'heat_input': 'heat_input({region}, watts={watts})',
    'heat_generation': 'heat_generation({volume}, watts={watts})',
    'convection': 'convection({region}, coefficient={h}, ambient={ambient})',
    'inlet': 'inlet({region}, speed={speed})',
    'outlet': 'outlet({region})',
    'wall': 'wall({region})',
    'slip': 'slip({region})',
}

# What a group of primitives makes (the kind: see fieldes.kinds), for the icon beside each entry
GROUP_KINDS = {'1D': 'curve', '3D': 'solid', '2D': 'profile', 'Points': 'point', 'Surfaces': 'surface', 'Fields': 'field',
               'Materials': 'material', 'Fluids': 'material', 'Supports and loads': 'conditions',
               'Thermal conditions': 'conditions', 'Flow conditions': 'conditions',
               'Strut cells': 'cell', 'TPMS cells': 'cell', 'Planar cells': 'cell', 'Custom cells': 'cell'}
# What an operation makes, where it is not what its group makes (the operations of the lattice section make lattices, but random_points makes points)
OPERATION_KINDS = {'random_points': 'points'}

# (function, group, template, takes a second model: True = needs one, 'optional' = takes the other selected models if there are any)
OPERATIONS = [
    ('offset', 'Offsets and walls', 'offset({body}, {t})', 'optional'),
    ('shell', 'Offsets and walls', 'shell({body}, {t})', 'optional'),
    ('thicken', 'Offsets and walls', 'thicken({body}, {t})', 'optional'),
    ('shell_inside', 'Offsets and walls', 'shell_inside({body}, {t})', 'optional'),
    ('shell_outside', 'Offsets and walls', 'shell_outside({body}, {t})', 'optional'),
    ('shell_centered', 'Offsets and walls', 'shell_centered({body}, {t})', 'optional'),
    ('offset_exact', 'Offsets and walls', 'offset_exact({body}, {t})', 'optional'),
    ('shell_exact', 'Offsets and walls', 'shell_exact({body}, {t})', 'optional'),
    ('smooth', 'Rounding', 'smooth({body}, {t}, {n})', 'optional'),
    ('round_edges', 'Rounding', 'round_edges({body}, {t})', 'optional'),
    ('fillet', 'Rounding', 'fillet({body}, {t})', 'optional'),
    ('move', 'Moving', 'move({body}, ({dx}, {dy}, {dz}))', False),
    ('rotate_z', 'Moving', 'rotate_z({body}, {angle_rad})', False),
    ('scale_xyz', 'Moving', 'scale_xyz({body}, ({sx}, {sy}, {sz}))', False),
    ('reflect_x', 'Moving', 'reflect_x({body})', False),
    ('symmetric_x', 'Moving', 'symmetric_x({body})', False),
    ('symmetric_y', 'Moving', 'symmetric_y({body})', False),
    ('symmetric_z', 'Moving', 'symmetric_z({body})', False),
    ('array_x', 'Repeating', 'array_x({body}, {n}, {s})', False),
    ('array_xy', 'Repeating', 'array_xy({body}, {nx}, {ny}, ({dx}, {dy}))', False),
    ('array_polar_z', 'Repeating', 'array_polar_z({body}, {n})', False),
    ('union', 'Combining', 'union({body}, {other})', True),
    ('difference', 'Combining', 'difference({body}, {other})', True),
    ('intersection', 'Combining', 'intersection({body}, {other})', True),
    ('exclude', 'Combining', 'exclude({body}, {other})', True),
    # A surface chosen by bodies: the first selected model's surface where the others meet it; one model alone: all of its surface
    # ('optional': the others are not needed, but taken when several models are selected: {others} is ", a, b" or nothing)
    ('surface_from_bodies', 'Surfaces', 'surface_from_bodies({body}{others})', 'optional'),
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
    ('body_from_field', 'Field math', 'body_from_field({body})', False),
    # Lattices (the cell is a first choice to change in the script: cell_periodic('octet'), ...; a cell that is selected with the body is the
    # cell).  A foam is not made of a cell but around points: random_points makes them in the body, and the lattice made of the points fills it
    ('lattice', 'Lattices', "lattice({body}, cell_periodic('{cell}'), cell_size={cs}, thickness={ct})", 'optional'),
    ('lattice_surface_conform', 'Lattices',
     "lattice_surface_conform({body}, cell_periodic('{cell}'), cell_size={cs})", 'optional'),
    ('random_points', 'Lattices', 'random_points({body}, spacing={cs})', False),
    ('voronoi_lattice', 'Lattices', 'voronoi_lattice({body})', False),
    ('delaunay_lattice', 'Lattices', 'delaunay_lattice({body})', False),
    # Importing: the surface of any model as the exact distance to its meshed surface (a file is imported from the menu of
    # empty space, Import model...)
    ('tessellate', 'Importing', 'tessellate({body})', False),
    # Measuring: both are in the first menu of a body (QUICK), not under a group
    ('center', 'Measuring', 'center({body})', False),
    ('bounding_box', 'Measuring', 'bounding_box({body})', False),
    # A field made of a point: how far each place is from it (in the first menu of a point, and only there)
    ('distance_to_point', 'Fields', 'distance_to_point({body})', False),
    # Simulations: the model is the part (the fluid, for the flow); every kind of condition the analysis takes is an input of its own, written
    # as a placeholder (`supports=...`, `inlets=...`) -- nothing is made up to stand there: the statement waits until they are given.  The
    # conditions that are selected with the body are written in the input of their kind.  The completion writes the inputs from the signature
    ('static_analysis', 'Simulations', 'static_analysis({body})', 'optional'),
    ('modal_analysis', 'Simulations', 'modal_analysis({body})', 'optional'),
    ('topology_optimization', 'Simulations', 'topology_optimization({body})', 'optional'),
    ('thermal_analysis', 'Simulations', 'thermal_analysis({body})', 'optional'),
    ('fluid_analysis', 'Simulations', 'fluid_analysis({body})', 'optional'),
    # The optimisations that change a body for the best heat or flow: a thermal one (the part, its four thermal inputs) and a flow one (the body
    # and the fluid domain it sits in, the three flow inputs)
    ('thermal_topology_optimization', 'Simulations', 'thermal_topology_optimization({body})', 'optional'),
    ('flow_topology_optimization', 'Simulations', 'flow_topology_optimization({body})', 'optional'),
]


# (operations that are in the first menu of a body, not under a group: used all the time)
QUICK = ('center', 'bounding_box', 'distance_to_point')


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
        'primitives': [dict({'name': n, 'group': g, 'type': GROUP_KINDS.get(g, 'solid'), 'with_bodies': n in BODY_CONDITIONS or n in FROM_SELECTION,
                             'from_selection': n in FROM_SELECTION},
                            **({'label': LABELS[n]} if n in LABELS else {}), **({'section': SECTIONS[g]} if g in SECTIONS else {}),
                            **({'doc': FROM_SELECTION[n]} if n in FROM_SELECTION else {}))
                       for n, g, _ in PRIMITIVES]
                      + [{'name': b['name'], 'group': 'Custom blocks', 'type': 'block', 'doc': b['doc']}
                         for b in blocks if b['primitive']],
        # (`other`: takes the other selected models; `needs_other`: is not offered without a second model)
        'operations': [dict({'name': n, 'group': g, 'other': bool(o), 'needs_other': o is True, 'quick': n in QUICK},
                            **({'section': SECTIONS[g]} if g in SECTIONS else {}), **({'hint': HINTS[n]} if n in HINTS else {}),
                            **({'type': OPERATION_KINDS[n]} if n in OPERATION_KINDS else
                                                                                       {'type': 'lattice'} if g == 'Lattices' else {}))
                       for n, g, _, o in OPERATIONS]
                      + [{'name': b['name'], 'group': 'Custom blocks', 'other': False, 'type': 'block', 'doc': b['doc']}
                         for b in blocks if b['operation']],
    })


_REGION_TYPES = ('solid', 'profile', 'field', 'surface', 'lattice')          # (what can say where a condition acts: a body, a field, a surface)


def _others(r):
    ''' The other selected models as [{name, type, role, cls}] (the request's `others`; the names of `other` when there are none) '''
    others = r.get('others')
    if others is None:
        others = [{'name': n.strip()} for n in (r.get('other') or '').split(',') if n.strip()]
    return others


def _where(models):
    ''' Those of the selected models that say where a condition acts: bodies, fields, surfaces (a model that has not run has no type yet: it may) '''
    return [o for o in models if (o.get('type') in _REGION_TYPES or not o.get('type')) and o.get('role') in (None, '')]


def _region_arg(regions):
    ''' The argument that gives a condition its regions, named: `region=a`, or `region=[a, b]` for several.  The conditions have no body in
        them: what is selected is where they act (the simulation is given the body) '''
    names = [o['name'] for o in regions]
    return 'region=' + (names[0] if len(names) == 1 else '[' + ', '.join(names) + ']')


def _materials(others):
    return [o['name'] for o in others if o.get('type') == 'material' and o.get('cls') != 'Fluid']


def _fluids(others):
    return [o['name'] for o in others if o.get('type') == 'material' and o.get('cls') == 'Fluid']


# ---- What an entry of a menu takes, and whether the models selected are that.
#
# Every model is classified once (_tag): by its kind, and for conditions by the input of a simulation it goes in (`supports`, `loads`,
# `inlets` ...: the library says -- see fieldes.stdlib.fea._SLOTS) and for a material whether it is a fluid.  An entry says which of those it
# takes: a list of SIGNATURES -- the tags it accepts, and the tags it must be given at least one of.  A selection fits an entry when all of
# its models are accepted by ONE signature and the signature's needs are met; an entry it does not fit is greyed out in the menu (nothing is
# said: it is not offered).  A model of no known kind (not run yet) fits anything, and so does an entry that declares nothing (a custom block).
_SHAPES = frozenset(('solid', 'profile', 'curve', 'import', 'simulation', 'field', 'surface', 'point', 'lattice'))      # (what a geometric operation works on)
_PART = frozenset(('solid', 'profile', 'import', 'simulation', 'lattice'))                                 # (what a simulation is made for)
_REGIONS = frozenset(_REGION_TYPES)                                                               # (what a condition is made of: where it acts)

# The simulations of the menu: each takes the body, the conditions of the kinds it has an input for, and a material (a fluid for a flow)
_SIMULATIONS = ('static_analysis', 'modal_analysis', 'topology_optimization', 'thermal_analysis', 'fluid_analysis',
                'thermal_topology_optimization', 'flow_topology_optimization')
_FLOWS = ('fluid_analysis', 'flow_topology_optimization')          # (what has a fluid, not a material)
# (the conditions of the menu are made of the regions selected: bodies, fields and surfaces)
_TAKES_CONDITION = [(_REGIONS, [])]


def _slots_of(name):
    ''' The inputs of the analysis `name` that are kinds of conditions -- its parameters that are named like one (`supports`, `loads`,
        `inlets`, `outlets` ...), in the order it lists them: what the library says, not a list kept here '''
    try:
        import fieldes
        from fieldes.stdlib.fea import _SLOTS
        return [p for p in inspect.signature(getattr(fieldes, name)).parameters if p in _SLOTS]
    except Exception:
        return []


def _takes(name):
    if name == 'distance_to_point':
        return [({'point'}, [])]                # (the distance from a point: a point is what it is made of)

    # (a lattice is made of a model, and of the cell selected with it; points are made in a model; a foam is made around points, and they are all
    # it takes -- they know the model it fills)
    if name in ('lattice', 'lattice_surface_conform'):
        return [(_SHAPES | {'cell'}, [_SHAPES])]
    if name == 'random_points':
        return [(_SHAPES, [_SHAPES])]
    if name in ('voronoi_lattice', 'delaunay_lattice'):
        return [({'points'}, [{'points'}])]
    # (a simulation takes a part, the conditions of the kinds it has an input for and a material -- and needs none of them: what is not selected
    # is a placeholder in the call)
    if name in _SIMULATIONS:
        return [(_PART | set(_slots_of(name)) | ({'fluid'} if name in _FLOWS else {'material'}), [])]
    return None


def _tag(m):
    ''' What a selected model is, for what an entry takes: its kind -- solid, profile, field, surface, point, simulation, cell, import --
        and for conditions the input they go in (supports, loads, inlets ...) and for a material whether it is a fluid.  None when it is not
        known (a model that has not run) '''
    kind, role, cls = m.get('type') or '', m.get('role') or '', m.get('cls') or ''
    if kind == 'conditions':
        return role or 'conditions'
    if kind == 'material':
        return 'fluid' if cls == 'Fluid' else 'material'
    return kind or None


def _selection_fits(kind, name, models):
    ''' Whether the models selected (each {type, role, cls}) are what the entry takes (see above) '''
    if kind == 'primitive':
        signatures = _TAKES_CONDITION if name in BODY_CONDITIONS else None
    else:
        group = next((g for n, g, _, _ in OPERATIONS if n == name), None)
        signatures = _takes(name) or ([(_SHAPES, [])] if group else None)
    tags = [t for t in map(_tag, models) if t]
    if not signatures or not tags:
        return True
    if name in _SIMULATIONS:
        # (one input takes one part and one material: a second one has nowhere to go -- the conditions are lists, they take any number)
        if sum(t in _PART for t in tags) > (2 if name in _FLOWS else 1):
            return False
        if sum(t in ('material', 'fluid') for t in tags) > 1:
            return False
    return any(all(t in accepts for t in tags) and all(any(t in need for t in tags) for need in needs) for accepts, needs in signatures)


def _body_first(body, info, others):
    ''' The first model selected is what an operation works on -- unless it is itself something the operation is made of: a condition or a
        material.  Those are conditions in whatever order the models were selected, and the body is then the first solid or profile among
        the others ('' when none was selected).  Returns (body, its info, the others) '''
    if info.get('type') not in ('conditions', 'material'):
        return body, info, others
    first = {'name': body, 'type': info.get('type'), 'role': info.get('role') or '', 'cls': info.get('cls') or ''}
    main = next((o for o in others if o.get('type') in ('solid', 'profile', 'lattice')), None)
    if main is None:
        return '', {}, [first] + list(others)
    return (main['name'], {'type': main.get('type'), 'role': '', 'cls': main.get('cls') or '', 'part': ''},
            [first] + [o for o in others if o is not main])


def _from_selection(name, models, values):
    ''' The call of a New entry that is made of the models selected -- the points a sketch or a curve goes through (in the order they were selected), a
        curve a sketch is the outline of, the 2D shape an extrusion pulls and the curve it is swept along -- or None when what is selected is not what
        the entry takes (it is made from its own template then, with new points) '''
    points = [m['name'] for m in models if m.get('type') == 'point']
    curves = [m['name'] for m in models if m.get('type') == 'curve']
    profiles = [m['name'] for m in models if m.get('type') == 'profile']
    if name == 'sketch':
        if len(points) >= 3:
            return 'sketch([%s])' % ', '.join(points)
        if curves and not points:
            return 'sketch(%s)' % curves[0]
    elif name == 'curve':
        if len(points) >= 2:
            return 'curve([%s])' % ', '.join(points)
    elif name == 'extrude':
        if profiles:
            return 'extrude(%s, 0, %s)' % (profiles[0], values.get('s', 10))
    elif name == 'path_extrude':
        if profiles and curves:
            return 'path_extrude(%s, %s)' % (profiles[0], curves[0])
        if profiles and len(points) >= 2:
            # (the shape and the points of the path: the points make a curve of their own, which is shown and edited like any other)
            var = values.get('var') or 'path_extrude_1'
            return '%s_path = curve([%s])\n%s_path%spath_extrude(%s, %s_path)' % (var, ', '.join(points), var, PRELUDE, profiles[0], var)
    return None


def _build_operation(name, v, others):
    ''' The text of an operation that is given the models selected with the body (see OPERATIONS: 'optional'), or None when they say
        nothing it can use -- the template is written then '''
    if name in _SIMULATIONS:
        body, info, others = _body_first(v['body'], v.get('body_info') or {}, others)
        v = dict(v, body=body, body_info=info)
    body = v['body']
    if name in ('lattice', 'lattice_surface_conform'):
        # (the first model selected is the one the lattice is made in -- unless it is a cell: that is what it is made of)
        info = v.get('body_info') or {}
        if info.get('type') == 'cell':
            main = next((o for o in others if o.get('type') in _SHAPES), None)
            if main is not None:
                others = [{'name': body, 'type': info.get('type')}] + [o for o in others if o is not main]
                body = main['name']
                v = dict(v, body=body)
    if name == 'lattice':
        # (a cell that is selected with the body is the cell; a field is the thickness)
        cells = [o['name'] for o in others if o.get('type') == 'cell']
        fields = [o['name'] for o in others if o.get('type') == 'field']
        if cells or fields:
            v = dict(v)
            if fields:
                v['ct'] = fields[0]
            return ('lattice({body}, %s, cell_size={cs}, thickness={ct})' % (cells[0] if cells else "cell_periodic('{cell}')")).format(**v)
    if name == 'lattice_surface_conform':
        cells = [o['name'] for o in others if o.get('type') == 'cell']
        if cells:
            return 'lattice_surface_conform({body}, %s, cell_size={cs})'.format(**v) % cells[0]
    slot = FIELD_SLOTS.get(name)
    if slot:
        fields = [o['name'] for o in others if o.get('type') == 'field']
        if not fields:
            return None
        for n, _, template, _ in OPERATIONS:
            if n == name:
                return template.format(**dict(v, **{slot: fields[0]}))
        return None
    if name in _SIMULATIONS:
        # The body, then the conditions that were selected, each in the input of its kind -- every input that is not given is a placeholder
        # (the completion writes it), as is every other argument the call cannot do without, the body too when none was selected
        # (the flow optimisation has two: the body and the fluid domain it sits in, in the order they were selected)
        try:
            import fieldes
            params = list(inspect.signature(getattr(fieldes, name)).parameters)
        except Exception:
            params = []
        parts = ([body] if body else []) + [o['name'] for o in others if o.get('type') in ('solid', 'profile', 'import', 'simulation', 'lattice')]
        args, missing = [], False
        for k in range(2 if name in _FLOWS else 1):
            if k < len(parts):
                args.append(parts[k] if not missing else '%s=%s' % (params[k] if k < len(params) else 'part', parts[k]))
            else:
                missing = True
                args.append('%s=...' % (params[k] if k < len(params) else 'part'))
        by_slot = {}
        for o in others:
            if o.get('type') == 'conditions' and o.get('role'):
                by_slot.setdefault(o['role'], []).append(o['name'])
        for s in _slots_of(name):
            if s in by_slot:
                args.append('%s=[%s]' % (s, ', '.join(by_slot[s])))
        if name in _FLOWS:
            fluid = _fluids(others)
            if fluid:
                args.append('fluid=%s' % fluid[0])
        else:
            material = _materials(others)
            if material:
                args.append('material=%s' % material[0])
        return '%s(%s)' % (name, ', '.join(args))
    return None


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
        place and size of the click when there are none): `es`, the element size (a twentieth of its longest side), and a lattice's
        cell size and thickness.  (No geometry is made from them: where a condition acts is a placeholder until the user says) '''
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

    return {'es': _num(_nice(longest / 20.0)),
            # (a lattice's cells: a tenth of the model across, their members an eighth of that thick)
            'cs': _num(_nice(longest / 10.0)), 'ct': _num(_nice(longest / 10.0) / 8.0)}


def _values(r):
    ''' What the templates are filled from: the place, the size, the models, and the boxes and numbers worked out from them '''
    name = r['name']
    s = _nice(float(r.get('scale', 10.0)))
    step = _step(s)

    def snap(v):
        return round(float(v) / step) * step

    x, y, z = snap(r.get('x', 0.0)), snap(r.get('y', 0.0)), snap(r.get('z', 0.0))
    h = s / 2.0
    values = {
        'x': _num(x), 'y': _num(y), 'z': _num(z), 's': _num(s), 'h': _num(h), 't': _num(s / 10.0), 'q': _num(s / 6.0),
        'xm': _num(x - h), 'xp': _num(x + h), 'ym': _num(y - h), 'yp': _num(y + h), 'zm': _num(z - h), 'zp': _num(z + h),
        'body': r.get('body', ''), 'other': r.get('other', ''), 'var': r.get('var') or (name + '_1'),
    }
    values['others'] = ', ' + values['other'] if values['other'] else ''       # (for a call that may have no other model)
    values.update(_regions(r, s))
    values['body_info'] = r.get('body_info')
    return values


def call(request):
    ''' The text of the call a menu entry writes, complete and laid out (fieldes.completion: what the call needs and was not given is a
        placeholder, every other argument is written with its default, one argument to a line when it does not fit).  `request` is JSON:
        {kind: 'primitive' | 'operation', name, x, y, z (the place), scale (mm spanned by about a hundred pixels there), body, other (names
        or expressions of the models), var (the name the model will get), indent (how far the statement is indented) } '''
    r = json.loads(request) if isinstance(request, str) else request
    text = _call(r)
    head, marker, expression = text.rpartition(PRELUDE)
    from fieldes import completion
    indent = int(r.get('indent', 0))
    column = indent + len(r.get('var') or (r['name'] + '_1')) + 3          # (the statement is `var = call`)
    if _makes_up_nothing(r):
        # (an entry that had no model to build from was written from its template: it keeps the models and drops what the template built
        # -- `static_analysis(part, conditions=...)`, not conditions of its own)
        expression = completion.without_made_up_values(expression, structure=bool(r.get('_templated')))
    expression = completion.complete_call(expression, column, indent)
    return head + marker + expression


KEEPS_ITS_TEMPLATE = ('lattice', 'lattice_surface_conform', 'random_points', 'voronoi_lattice', 'delaunay_lattice')

# (the shapes of the first groups are made where the user clicks, as big as the view shows: the place and the size are what the click says.
# Everything else a menu writes is made of models and calls: the numbers in them are for the user to say)
_PLACED_GROUPS = ('1D', '3D', '2D', 'Points', 'Surfaces', 'Fields', 'Materials', 'Fluids',
                  'Strut cells', 'TPMS cells', 'Planar cells', 'Custom cells')


def _makes_up_nothing(r):
    ''' Whether the entry writes no number or text of its own: every operation, and the conditions of a simulation.  (The lattice operations
        keep the cell they write and the sizes worked out from the model -- a tenth of it across for a cell, an eighth of that for its thickness:
        what they are is what they say) '''
    if r['kind'] == 'operation':
        return r['name'] not in KEEPS_ITS_TEMPLATE
    if r['kind'] == 'primitive':
        for n, group, _ in PRIMITIVES:
            if n == r['name']:
                return group not in _PLACED_GROUPS or n == 'material'
    return False


def _call(r):
    kind, name = r['kind'], r['name']
    values = _apply_params(name, _values(r))
    others = _others(r)
    selected = ([dict(r.get('body_info') or {}, name=r['body'])] if r.get('body') else []) + list(others)
    if (kind == 'operation' or (name in BODY_CONDITIONS and selected)) and not _selection_fits(
            kind, name, ([r['body_info']] if r.get('body') and r.get('body_info') else []) + list(others)):
        # (the menu greys the entry out for such a selection, so this is not reached from it)
        raise ValueError('%s does not take what is selected' % name)
    if kind == 'primitive' and name in FROM_SELECTION and selected:
        built = _from_selection(name, selected, values)
        if built:
            return built
    if kind == 'primitive' and name in BODY_CONDITIONS and selected:
        # a condition made of the regions selected: whatever is selected says where it acts -- a surface, a field, a body.  The conditions
        # have no body in them: the simulation is given the body
        regions = _where(selected)
        if not regions:
            raise ValueError(tr('%s acts where a surface, a field or a body says: select one or several of those') % name)
        return BODY_CONDITIONS[name].format(region=_region_arg(regions), volume=_region_arg(regions), **values)
    if kind == 'operation' and (values['body'] or name in _SIMULATIONS) and (r.get('others') is not None or r.get('body_info')
                                                                             or name in _SIMULATIONS):
        built = _build_operation(name, values, others)
        if built:
            return built
    if kind == 'primitive':
        for n, _, template in PRIMITIVES:
            if n == name:
                r['_templated'] = True
                return template.format(**values)
    elif kind == 'operation':
        for n, _, template, _ in OPERATIONS:
            if n == name:
                # (nothing to work on, or no second model: a placeholder stands there -- the run stops before the statement until a
                # model is put in its place)
                if not values['body']:
                    values = dict(values, body='...')
                if '{other}' in template and not values['other']:
                    values = dict(values, other='...')
                r['_templated'] = True
                return template.format(**values)
    for b in _blocks():                 # (a custom block: its call, with its other arguments left to their defaults)
        if b['name'] == name and kind == 'primitive' and b['primitive']:
            return name + '()'
        if b['name'] == name and kind == 'operation' and b['operation']:
            return '%s(%s)' % (name, values['body'] or '...')
    raise ValueError(tr('the menu has no %s called %s') % (kind, name))
