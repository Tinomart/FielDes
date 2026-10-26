'''
What the viewport's context menus create: the library's primitives and basic operations.

Right-clicking empty space in the viewport offers Primitives -- New 3D shape / 2D shape / 1D shape / point / surface / field, the most basic of each (BASIC), placed where the
cursor is -- Parametric shapes (the rest of the shapes the library can make with no argument, by kind) and Operations (the operations, Lattice, Simulation); right-clicking a body offers the same, with that body passed in.  The
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


# The materials and the fluids the library has ready (variables of fieldes.stdlib.fea and .fluid)
MATERIAL_PRESETS = ('steel', 'stainless_steel', 'aluminium', 'titanium', 'pla', 'petg', 'abs_plastic', 'nylon')
FLUID_PRESETS = ('water', 'air', 'oil', 'glycerol')


def _materials():
    ''' The materials the menus offer: the presets of the library, written out with their numbers (so that they are there to
        change), a Material of your own, and the fluids '''
    out = []
    try:
        from fieldes.stdlib import fea, fluid
        for name in MATERIAL_PRESETS:
            m = getattr(fea, name)
            out.append((name, 'Materials', 'Material(%r, %s, %s, %s, %s, %s, %s)' % (
                m.name, _g(m.E), _g(m.nu), _g(m.density), _g(m.yield_strength), _g(m.conductivity), _g(m.expansion))))
        out.append(('material', 'Materials', "Material('{mname}', {mE}, {mnu}, {mdens}, {myield})"))
        for name in FLUID_PRESETS:
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
SECTIONS = {'Strut cells': 'periodic', 'TPMS cells': 'periodic', 'Planar cells': 'periodic', 'Custom cells': 'periodic', 'Periodic lattices': 'periodic',
            'Non-periodic lattices': 'non_periodic'}
# The categories of the Operations menu those sections make, in the order they are listed: lattices made of cells that repeat, and lattices made around points or over a surface,
# that do not.  (A category is in the menu when something in it is valid: see View::fillOperationsMenu)
SECTION_MENUS = [{'name': 'periodic', 'label': tr('Periodic lattices'), 'type': 'lattice'},
                 {'name': 'non_periodic', 'label': tr('Non-periodic lattices'), 'type': 'points'}]

# What the Lattice menu says of an entry when the mouse rests on it
HINTS = {
    'lattice': tr('With a cell selected with the model: that cell.  Otherwise a first cell to change in the script.'),
    'lattice_surface_conform': tr('With a cell selected with the model: that cell.  Otherwise a first cell to change in the script.'),
    'random_points': tr("Points made in the model, drawn as dots: how far apart they are on average, how random, the nearest and the farthest two "
                        "may be.  A foam is made around them: select them and choose voronoi_lattice or delaunay_lattice"),
    'surface_lattice': tr('A random net of beams over the whole surface of the model: a lattice of one layer, that does not repeat.  How far apart its nodes are, '
                          'and how thick its beams are, are the numbers to change in the script'),
    'voronoi_lattice': tr('A foam: the edges of the Voronoi cells around the selected points, as beams, filling the model the points were made in.  '
                          'Select points first (random_points)'),
    'delaunay_lattice': tr('A stochastic truss: beams along the Delaunay edges between the selected points, filling the model the points were '
                           'made in.  Select points first (random_points)'),
    'instance': tr('A linked copy of the selected model: the same shape, following every change of it, that can only be moved and rotated (Ctrl+D)'),
}

# A profile or a curve made from the menu: its points are made with it (each a model of its own, drawn as a dot, with the gizmo that moves and
# rotates it), in the horizontal plane at the place of the click
def _gizmoed_point(name, x, y, z):
    ''' The statements of a point that is a model of its own, with its gizmo (so that it can be dragged) '''
    return ('%s = point(\n    %s, %s, %s,\n    position=(var(0), var(0), var(0)),\n    rotation=(var(0), var(0), var(0)),\n'
            '    scale=(var(1), var(1), var(1)),\n)\n%s\n' % (name, x, y, z, name))


def _points(*places, base='{var}'):
    ''' The points a profile or a line made from the menu is made of: each a model of its own, with its gizmo (so that it can be dragged, and the face or the line
        with all of them), written above the call.  `places` are the (x, y, z) texts of the template '''
    out = []
    for k, (x, y, z) in enumerate(places, 1):
        out.append(_gizmoed_point('%s_p%d' % (base, k), x, y, z))
    return ''.join(out), ', '.join('%s_p%d' % (base, k) for k in range(1, len(places) + 1))


_PROFILE_POINTS, _PROFILE_LIST = _points(('{xm}', '{ym}', '{z}'), ('{xp}', '{ym}', '{z}'), ('{xp}', '{yp}', '{z}'), ('{xm}', '{yp}', '{z}'))
_PROFILE = _PROFILE_POINTS + '#@@#\nprofile([' + _PROFILE_LIST + '])'

_LINE_POINTS, _LINE_LIST = _points(('{xm}', '{y}', '{z}'), ('{x}', '{yp}', '{z}'), ('{xp}', '{y}', '{z}'))
_POLYLINE = _LINE_POINTS + '#@@#\npolyline([' + _LINE_LIST + '])'
_SPLINE = _LINE_POINTS + '#@@#\nspline([' + _LINE_LIST + '])'

# What an entry can be made OF the models selected, as DATA -- the entries that are made of what is selected, each with the words for its tooltip and the WAYS it is made, tried
# in the order they are listed: (the kinds of model it is made of, the least and the most of them (None: any number), the call it is written as -- `{names}` is those models in
# the order they were selected, `{name}` the first).  A way is taken when the selection has the right number of models of its kinds and none of the kinds of the ways before it;
# with none of them selected the entry is made from its own template (new points to drag).  The menu, its tooltips and the call are all read from this: a function the library
# can make of a kind of model is one more row here, and nothing else in the menus names it
FROM_SELECTION = {
    'profile': (tr('A 2D shape from points or from a surface.  With points selected: made from them, in the order you selected them.  With a surface selected: its '
                  'outline (a flat face exactly; a surface that is not flat is projected onto a plane).  With none: four new points to drag.  '
                  'Press its edge and drag to pull a point out of it; double-click a point to make it a corner or a spline point'),
               [({'point'}, 3, None, 'profile([{names}])'), ({'curve'}, 1, None, 'profile({name})'), ({'surface'}, 1, None, 'profile({name})')]),
    'polyline': (tr('A set of lines through points.  With points selected: made from them, in the order you selected them.  With none: three new points to '
                    'drag.  Press the line and drag to pull a point out of it; double-click a point to make it a corner or a spline point'),
                 [({'point'}, 2, None, 'polyline([{names}])')]),
    'spline': (tr('A smooth curve through points.  With points selected: made from them, in the order you selected them.  With none: three new points to '
                  'drag.  Press the curve and drag to pull a point out of it; double-click a point to make it a corner or a spline point'),
               [({'point'}, 2, None, 'spline([{names}])')]),
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
    ('prism', '3D', 'prism(({xm}, {ym}), ({xp}, {ym}), ({x}, {yp}), {zm}, {zp})'),
    ('capsule', '3D', 'capsule({q}, {s}, ({x}, {y}, {zm}))'),
    ('tube', '3D', 'tube({h}, {q}, {s}, ({x}, {y}, {zm}))'),
    # (an M8 is 8 mm across whatever the view: its length is not a tenth of the view's span -- a few millimetres, zoomed in, and a flat disc with hardly a turn of thread -- but two and a half diameters, a rod)
    ('threaded_rod', '3D', "threaded_rod('M8', 20, ({x}, {y}, {zm}))"),
    ('low_level_body', '3D', _LOW_LEVEL_BODY),
    ('circle', '2D', 'circle({h}, ({x}, {y}))'),
    ('rectangle', '2D', 'rectangle(({xm}, {ym}), ({xp}, {yp}))'),
    ('rounded_rectangle', '2D', 'rounded_rectangle(({xm}, {ym}), ({xp}, {yp}), {r})'),
    ('slot', '2D', 'slot({s}, {h}, ({x}, {y}))'),
    ('ring', '2D', 'ring({h}, {q}, ({x}, {y}))'),
    ('polygon', '2D', 'polygon({h}, {n}, ({x}, {y}))'),
    ('triangle', '2D', 'triangle(({xm}, {ym}), ({xp}, {ym}), ({x}, {yp}))'),
    ('profile', '2D', _PROFILE),
    ('polyline', '1D', _POLYLINE),
    ('spline', '1D', _SPLINE),
    ('point', 'Points', 'point({x}, {y}, {z})'),
    ('symmetry_planes', 'Symmetry', 'symmetry_planes(planes=1, kind=\'axis\', centre=({x}, {y}, {z}), axis=(0, 0, 1))'),
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
    # What a manufacturing check or shell is given: the rules, as a constraint of your own or one of the presets of a process
    ('manufacturing_constraint', 'Manufacturing constraints', 'manufacturing_constraint()'),
    ('fff_constraint', 'Manufacturing constraints', 'fff_constraint()'),
    ('cnc_constraint', 'Manufacturing constraints', 'cnc_constraint()'),
    ('molding_constraint', 'Manufacturing constraints', 'molding_constraint()'),
] + _materials() + [(n, g, t) for n, g, t, _l in _CELLS]

# The PRIMITIVES -- the most basic, low-level representation of each kind of thing: a sphere, a box, a cylinder, a cone, a torus, a pyramid and a prism for 3D; a circle, a rectangle, a triangle,
# and a polygon for 2D (a profile, a 2D shape made of points, is a Parametric shape); a polyline and a spline for 1D; a point; a plane, a sphere surface and a cylinder surface; a distance, a radial, a z and a noise field.  The menus show these under
# Primitives, apart.  Everything else the library can make with no argument -- a rounded box, a ring, a wave, noise, your own low-level logic, a custom block -- is not a primitive, however
# little it needs: it is a Parametric shape
BASIC = frozenset((
    'sphere', 'box', 'cylinder', 'cone', 'torus', 'pyramid', 'prism',
    'circle', 'rectangle', 'triangle', 'polygon',
    'polyline', 'spline',
    'point',
    'symmetry_planes',
    'plane', 'sphere_surface', 'cylinder_surface',
    'distance_to_point', 'radial_field', 'z_field', 'noise_field',
))

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
    'rotate': {'angle': 45},
    'linear_pattern': {'n': 3},
    'circular_pattern': {'n': 6},
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
    'thermal_analysis': {'temp': 20, 'watts': 5, 'es': '$es'},
    'fluid_analysis': {'speed': 100, 'es': '$es'},
    'mesh_volume': {'es': '$es'},
    'mesh_surface': {'es': '$es'},
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
GROUP_KINDS = {'1D': 'curve', '3D': 'solid', '2D': 'profile', 'Points': 'point', 'Surfaces': 'surface', 'Fields': 'field', 'Symmetry': 'symmetry',
               'Materials': 'material', 'Fluids': 'material', 'Supports and loads': 'conditions',
               'Thermal conditions': 'conditions', 'Flow conditions': 'conditions', 'Manufacturing constraints': 'conditions',
               'Strut cells': 'cell', 'TPMS cells': 'cell', 'Planar cells': 'cell', 'Custom cells': 'cell'}
# The domain of the groups of primitives that are about one (the categories of the Engineering menu: see fieldes.kinds); a material is of none -- an analysis of any
# domain takes one
PRIMITIVE_DOMAINS = {'Supports and loads': 'structural', 'Thermal conditions': 'thermal', 'Flow conditions': 'fluid', 'Fluids': 'fluid',
                     'Manufacturing constraints': 'manufacturing'}

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
    # A texture on the surface of a body: a surface that is selected with it (or a field, or a body) is where it goes
    ('texture', 'Texturing', 'texture({body})', 'optional'),
    ('smooth', 'Rounding', 'smooth({body}, {t}, {n})', 'optional'),
    ('round_edges', 'Rounding', 'round_edges({body}, {t})', 'optional'),
    ('fillet', 'Rounding', 'fillet({body}, {t})', 'optional'),
    ('move', 'Moving', 'move({body}, ({dx}, {dy}, {dz}))', False),
    ('rotate_z', 'Moving', 'rotate_z({body}, {angle_rad})', False),
    ('scale_xyz', 'Moving', 'scale_xyz({body}, ({sx}, {sy}, {sz}))', False),
    # CAD operations (see fieldes.stdlib.cad): turned about any axis, mirrored in any plane, patterns, revolved, cut by a plane, a hole
    ('rotate', 'Moving', 'rotate({body}, (0, 0, 1), {angle})', False),
    ('reflect', 'Moving', 'reflect({body})', False),
    ('mirror', 'Moving', 'mirror({body})', False),
    ('reflect_x', 'Moving', 'reflect_x({body})', False),
    ('symmetric_x', 'Moving', 'symmetric_x({body})', False),
    ('symmetric_y', 'Moving', 'symmetric_y({body})', False),
    ('symmetric_z', 'Moving', 'symmetric_z({body})', False),
    # A linked copy (Ctrl+D): the model itself, placed where its own gizmo says -- written with the model held by reference (`# shadow:`)
    ('instance', 'Repeating', 'instance({body})', False),
    ('array_x', 'Repeating', 'array_x({body}, {n}, {s})', False),
    ('array_xy', 'Repeating', 'array_xy({body}, {nx}, {ny}, ({dx}, {dy}))', False),
    ('array_polar_z', 'Repeating', 'array_polar_z({body}, {n})', False),
    ('linear_pattern', 'Repeating', 'linear_pattern({body}, {n}, ({s}, 0, 0))', False),
    ('circular_pattern', 'Repeating', 'circular_pattern({body}, {n})', False),
    # Extruding: a 2D shape pulled into a solid, straight or along a curve (a curve, or points that make one)
    ('extrude', 'Extruding', 'extrude({body}, 0, {s})', False),
    ('revolve', 'Extruding', 'revolve({body})', False),
    ('path_extrude', 'Extruding', 'path_extrude({body}, {other})', True),
    ('union', 'Combining', 'union({body}, {other})', True),
    ('difference', 'Combining', 'difference({body}, {other})', True),
    ('intersection', 'Combining', 'intersection({body}, {other})', True),
    ('cut_plane', 'Combining', 'cut_plane({body})', False),
    ('hole', 'Combining', 'hole({body}, center({body}), {t})', False),
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
    ('lattice', 'Periodic lattices', "lattice({body}, cell_periodic('{cell}'), cell_size={cs}, thickness={ct})", 'optional'),
    ('lattice_surface_conform', 'Periodic lattices',
     "lattice_surface_conform({body}, cell_periodic('{cell}'), cell_size={cs})", 'optional'),
    ('random_points', 'Non-periodic lattices', 'random_points({body}, spacing={cs})', False),
    ('voronoi_lattice', 'Non-periodic lattices', 'voronoi_lattice({body})', False),
    ('delaunay_lattice', 'Non-periodic lattices', 'delaunay_lattice({body})', False),
    ('surface_lattice', 'Non-periodic lattices', 'surface_lattice({body}, cell_size={cs}, thickness={ct})', False),
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
    ('topology_optimization', 'Simulations', 'topology_optimization({body})', False),
    ('thermal_analysis', 'Simulations', 'thermal_analysis({body})', 'optional'),
    ('fluid_analysis', 'Simulations', 'fluid_analysis({body})', 'optional'),
    # Meshing: the body as a mesh of elements for the solver of another program (its own category of the Engineering menu)
    ('mesh_volume', 'Simulations', 'mesh_volume({body})', 'optional'),
    ('mesh_surface', 'Simulations', 'mesh_surface({body})', 'optional'),
    ('solve', 'Simulations', "solve('heat', {body})", 'optional'),
    # What works over an analysis of any domain, and so is in no category of the Engineering menu: it is given the result of an analysis (a simulation) and
    # finds the best numbers for the var()s the model is made of
    ('parametric_optimization', 'Simulations', 'parametric_optimization({body})', False),
    # Manufacturing: the check of a part against constraints (where it breaks them, as colours on it) and the shell that changes the part until it keeps them
    ('manufacturing_analysis', 'Simulations', 'manufacturing_analysis({body})', 'optional'),
    ('manufacturing_optimization', 'Simulations', 'manufacturing_optimization({body})', 'optional'),
]


# (operations that are in the first menu of a body, not under a group: used all the time)
QUICK = ('center', 'bounding_box', 'distance_to_point')

# What is the first entry of the Engineering menu, on its own, above the categories of the domains: the optimisation that designs for an analysis of any domain -- a static, a modal, a thermal
# one, a flow -- and so is in the category of none of them
TOP = ('topology_optimization',)

# The "Python" menu: the most basic features of the language as blocks of plain code, written at the top level of the script, at the insertion point of the model tree (not
# made into a model: no `name = ...`, no display line).  The code is edited in the code editor; the tree shows the blocks and what is in them.  A body of `pass` is where the
# code of the block goes.  (name, label, what it is, the code)
PYTHON = [
    ('variable', 'variable', tr('A name for a number, a text or any value: it is used by its name further down'), 'value = 10'),
    ('if', 'if', tr('Runs the code under it only when the condition is true'), 'if value > 5:\n    pass'),
    ('if_else', 'if / else', tr('One code when the condition is true, another when it is not'), 'if value > 5:\n    pass\nelse:\n    pass'),
    ('for_range', 'for loop (a count)', tr('Runs the code under it once for each number 0, 1, 2 ... up to the count'), 'for i in range(5):\n    pass'),
    ('for_each', 'for loop (a list)', tr('Runs the code under it once for each item of a list'), 'for item in [1, 2, 3]:\n    pass'),
    ('while', 'while loop', tr('Runs the code under it again and again while the condition is true'), 'count = 0\nwhile count < 5:\n    count += 1'),
    ('function', 'function (def)', tr('Code with a name that takes values and gives one back: it is called by its name'), 'def my_function(x):\n    return x'),
    ('class', 'class', tr('A kind of object of your own, with functions of its own'), 'class MyClass:\n    def __init__(self):\n        pass'),
    ('list', 'list', tr('Several values under one name: values[0] is the first'), 'values = [1, 2, 3]'),
    ('list_comprehension', 'list from a loop', tr('A list made by a loop, in one line'), 'squares = [i * i for i in range(5)]'),
    ('dictionary', 'dictionary', tr('Values found by a name: settings["size"]'), 'settings = {"size": 10}'),
    ('try', 'try / except', tr('Runs the code, and another code when it fails'), 'try:\n    pass\nexcept Exception as error:\n    print(error)'),
    ('with', 'with (a file)', tr('Opens a file for the code under it, and closes it after'), 'with open("file.txt") as f:\n    text = f.read()'),
    ('import', 'import', tr('Brings in a module of Python: math, random ...'), 'import math'),
    ('print', 'print', tr('Writes a value to the message pane of the script'), 'print("hello")'),
]


def _blocks():
    ''' The custom blocks the menus offer (see fieldes.blocks): [{name, doc, operation, primitive}].  (The primitives of the user's own are blocks too, but they are in the menus under their kind:
        see user_primitives.catalog_entries) '''
    try:
        from fieldes import blocks
        return [b for b in blocks.info() if (b['operation'] or b['primitive']) and not b.get('user')]
    except Exception:
        return []


def catalog():
    ''' The lists as JSON for the menus: {"primitives": [{name, group, type}], "operations": [{name, group, other}]}; the custom
        blocks are in them too (group "Custom blocks", type "block") '''
    from fieldes.kinds import describe, describe_domains, DOMAIN_OF_FUNCTION
    from fieldes import user_primitives
    blocks = _blocks()
    basic = user_primitives.basic(BASIC)             # (what the user chose to show under Primitives: BASIC less what was taken out, and what was put in)
    given = user_primitives.labels()                 # (and the names the user gave the shapes of the library: the menus show those)
    return json.dumps({
        'kinds': describe(),
        'domains': describe_domains(),
        'primitives': [dict({'name': n, 'group': g, 'type': GROUP_KINDS.get(g, 'solid'), 'with_bodies': n in BODY_CONDITIONS or n in FROM_SELECTION, 'primitive': n in basic,
                             'from_selection': n in FROM_SELECTION},
                            **({'label': given[n]} if n in given else {'label': LABELS[n]} if n in LABELS else {}), **({'section': SECTIONS[g]} if g in SECTIONS else {}),
                            **({'domain': PRIMITIVE_DOMAINS[g]} if g in PRIMITIVE_DOMAINS else {}),
                            **({'doc': FROM_SELECTION[n][0]} if n in FROM_SELECTION else {}),
                            # (a 2D shape that is written with an x and a y only lies in the plane z = 0: the place it is made at is where the ray under the cursor meets that plane)
                            **({'flat': True} if g == '2D' and '{z}' not in t else {}))
                       for n, g, t in PRIMITIVES]
                      + user_primitives.catalog_entries()                # (the primitives of the user's own: a model of a script, kept in a file of theirs)
                      + [{'name': b['name'], 'group': 'Custom blocks', 'type': 'block', 'doc': b['doc']}
                         for b in blocks if b['primitive']],
        # (`other`: takes the other selected models; `needs_other`: is not offered without a second model)
        'operations': [dict({'name': n, 'group': g, 'other': bool(o), 'needs_other': o is True, 'quick': n in QUICK, 'top': n in TOP},
                            **({'section': SECTIONS[g]} if g in SECTIONS else {}), **({'hint': HINTS[n]} if n in HINTS else {}),
                            **({'domain': DOMAIN_OF_FUNCTION[n]} if g == 'Simulations' and n in DOMAIN_OF_FUNCTION else {}),
                            **({'type': OPERATION_KINDS[n]} if n in OPERATION_KINDS else
                                                                                       {'type': 'lattice'} if g in ('Periodic lattices', 'Non-periodic lattices') else {}))
                       for n, g, _, o in OPERATIONS]
                      + [{'name': b['name'], 'group': 'Custom blocks', 'other': False, 'type': 'block', 'doc': b['doc']}
                         for b in blocks if b['operation']],
        'python': [{'name': n, 'label': l, 'doc': d} for n, l, d, _code in PYTHON],
        'sections': SECTION_MENUS,
    })


_REGION_TYPES = ('solid', 'profile', 'field', 'surface', 'lattice')          # (what can say where a condition acts: a body, a field, a surface)


def _others(r):
    ''' The other selected models as [{name, type, role, cls}] (the request's `others`; the names of `other` when there are none) '''
    others = r.get('others')
    if others is None:
        others = [{'name': n.strip()} for n in (r.get('other') or '').split(',') if n.strip()]
    return others


_BODY_TYPES = ('solid', 'profile', 'import')            # (the models that are a body: a condition made of two of them has the first for its body and the rest for where it acts)


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
from fieldes.kinds import SHAPES as _SHAPES                                                       # (what a geometric operation works on)
_FOAMS = ('voronoi_lattice', 'delaunay_lattice')                                                 # (the lattices made around a set of points)
_PART = frozenset(('solid', 'profile', 'import', 'simulation', 'lattice'))                                 # (what a simulation is made for)
_REGIONS = frozenset(_REGION_TYPES)                                                               # (what a condition is made of: where it acts)

# The simulations of the menu are the entries of the group of that name: each takes its part(s), the conditions of the kinds it has an input for, and a material (a fluid for
# a flow).  What a simulation takes is read from its signature in the library, so that a new one needs no more than its row in the group
_SIMULATIONS = tuple(n for n, g, _t, _o in OPERATIONS if g == 'Simulations')
# (the conditions of the menu are made of the regions selected: bodies, fields and surfaces)
_TAKES_CONDITION = [(_REGIONS, [])]


def _parameters_of(name):
    ''' The names of the parameters of the library's function `name`, in order ([] when there is none of that name) '''
    try:
        import fieldes
        return list(inspect.signature(getattr(fieldes, name)).parameters)
    except Exception:
        return []


def _slots_of(name):
    ''' The inputs of the analysis `name` that are kinds of conditions -- its parameters that are named like one (`supports`, `loads`,
        `inlets`, `outlets` ...), in the order it lists them: what the library says, not a list kept here '''
    try:
        from fieldes.stdlib.fea import _SLOTS
    except Exception:
        return []
    return [p for p in _parameters_of(name) if p in _SLOTS]


def _is_flow(name):
    ''' Whether the analysis `name` has a fluid, not a material: its signature says so (`fluid=`) '''
    return 'fluid' in _parameters_of(name)


def _parts_of(name):
    ''' How many models the analysis `name` is made for -- a body, and for a flow the domain it sits in: the parameters of its signature that come before its conditions and
        before the first one that has a default (a model is always needed; what has a default -- the element size of a mesh, its element, its zones -- is not one) '''
    try:
        from fieldes.stdlib.fea import _SLOTS
        import fieldes
        parameters = list(inspect.signature(getattr(fieldes, name)).parameters.values())
    except Exception:
        return 1
    n = 0
    for p in parameters:
        if p.name in _SLOTS or p.name in ('material', 'fluid') or p.default is not p.empty:
            break
        n += 1
    return max(1, n)


def _takes(name):
    ''' What the operation `name` takes, as SIGNATURES (the kinds it accepts together, and the kinds it must be given at least one of): what the library function says of itself
        (`fn._takes`, see fieldes.kinds.takes -- no list of them is kept here), a simulation's from its signature, and None for a function that says nothing: it takes any shape '''
    try:
        import fieldes
        declared = getattr(getattr(fieldes, name, None), '_takes', None)
    except Exception:
        declared = None
    if declared:
        return declared
    # (a simulation takes a part, the conditions of the kinds it has an input for and a material -- and needs none of them: what is not selected
    # is a placeholder in the call)
    if name in _SIMULATIONS:
        return [(_PART | set(_slots_of(name)) | ({'fluid'} if _is_flow(name) else {'material'}), [])]
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
    if name in _FOAMS:
        # (a foam is made around points; with a body selected the entry makes the points too -- see _build_operation -- so it is offered for a body as well as for points)
        signatures = [(_SHAPES | {'point', 'points'}, [])]
    tags = [t for t in map(_tag, models) if t]
    if not signatures or not tags:
        return True
    if name in _SIMULATIONS:
        # (one input takes one part and one material: a second one has nowhere to go -- the conditions are lists, they take any number)
        if sum(t in _PART for t in tags) > _parts_of(name):
            return False
        if sum(t in ('material', 'fluid') for t in tags) > 1:
            return False
    return any(all(t in accepts for t in tags) and all(any(t in need for t in tags) for need in needs) for accepts, needs in signatures)


# What a New entry makes, for whether it fits the models selected.  A SHAPE -- a 3D shape, a 2D shape, a 1D shape, a point, a surface, a field -- is a PLACEMENT: it is made wherever the menu
# is, so it is valid for any selection (a line or a profile is made of the points, curve or surface that are selected when they fit, and of new points when not).  Everything else is valid
# only when the selection has a use for it: a condition when the selection says where it acts (a region), and one that is only something other functions take as an input -- a material, a
# fluid, a cell, a load -- when some function that says what it takes (see fieldes.kinds.takes; a simulation, by its signature) takes the selection together with it.  Nothing here names an entry
_PLACEMENT_KINDS = frozenset(('solid', 'profile', 'curve', 'point', 'surface', 'field'))


def _primitive_fits(name, models):
    ''' Whether the New entry `name` is valid for the models selected (each {type, role, cls}); anything is valid for none '''
    if not models:
        return True
    group = next((g for n, g, _t in PRIMITIVES if n == name), None)
    kind = GROUP_KINDS.get(group, 'solid')
    if kind in _PLACEMENT_KINDS:
        return True             # (placed anywhere -- a line or a profile too: made of the points, curve or surface selected when they fit (see FROM_SELECTION), of new points when not)
    if name in BODY_CONDITIONS:
        return bool(_where(models))
    # (something to be given to another function: the kind, role and class of what it makes, as a selected model of it would be)
    made = {'type': kind}
    if kind == 'material':
        made['cls'] = 'Fluid' if group == 'Fluids' else 'Material'
    elif kind == 'conditions':
        try:
            from fieldes import outline
            made['role'] = outline._guess_role(name)
        except Exception:
            made['role'] = ''
    users = [n for n, g, _t, _o in OPERATIONS if _takes(n)]
    return any(_selection_fits('operation', n, list(models) + [made]) for n in users)


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
    ''' The call of a New entry that is made of the models selected (the ways it is made are data: see FROM_SELECTION) -- the points a profile or a curve goes through (in the
        order they were selected), a curve or a surface a profile is the outline of -- or None when what is selected is not what the entry takes (it is made from its own
        template then, with new points) '''
    ways = FROM_SELECTION.get(name, (None, []))[1]
    present = {m.get('type') for m in models}
    earlier = set()
    for kinds, least, most, template in ways:
        mine = [m['name'] for m in models if m.get('type') in kinds]
        if not earlier & present and len(mine) >= least and (most is None or len(mine) <= most):
            return template.format(names=', '.join(mine), name=mine[0])
        earlier |= kinds
    return None


def _own_point_names(shape_name):
    ''' The names of the points the model `shape_name` is made of (the corners of a profile): the variables of the last run that hold those very points '''
    try:
        from fieldes import runner
        names = runner.last_globals
        mine = {id(p) for p in (getattr(names.get(shape_name), 'points', None) or [])}
        return {n for n, value in names.items() if id(value) in mine}
    except Exception:
        return set()


def _build_operation(name, v, others):
    ''' The text of an operation that is given the models selected with the body (see OPERATIONS: 'optional'), or None when they say
        nothing it can use -- the template is written then '''
    if name in _SIMULATIONS:
        if any(n == name and t != '%s({body})' % name for n, _g, t, _o in OPERATIONS):
            return None             # (an entry whose template says more than the body -- `solve('heat', part)` -- is written from its template)
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
    if name in _FOAMS:
        # (the foam is made around points: the points that were selected, or -- with a body -- points made in it, written first, so that one entry makes the whole foam)
        info = v.get('body_info') or {}
        if info.get('type') in ('point', 'points') or any(o.get('type') in ('point', 'points') for o in others):
            return None
        if info.get('type') in _SHAPES or not info.get('type') and body:
            var = v.get('var') or '%s_1' % name
            return '%s_points = random_points(%s, spacing=%s)\n%s_points%s%s(%s_points)' % (var, body, v.get('cs', 8), var, PRELUDE, name, var)
    if name == 'texture':
        # (a surface, a field or a body that is selected with the body is where the texture goes)
        regions = [o['name'] for o in others if o.get('type') in ('surface', 'field', 'solid', 'profile', 'lattice')]
        if regions:
            return 'texture(%s, region=%s)' % (body, regions[0])
    if name == 'path_extrude':
        # (the 2D shape and the path, whichever was selected first; points make a curve of their own, which is shown and edited like any other)
        models = [dict(v.get('body_info') or {}, name=body)] + list(others)
        profile = next((m['name'] for m in models if m.get('type') == 'profile'), None)
        path = next((m['name'] for m in models if m.get('type') == 'curve'), None)
        # (the points a 2D shape is made of are not points of the path: selecting a profile together with the points along its path must not put the profile's own
        # corners into the path -- a path that goes round the profile's outline too is a sweep of dozens of pieces)
        own = _own_point_names(profile)
        points = [m['name'] for m in models if m.get('type') == 'point' and m['name'] not in own]
        if profile and path:
            return 'path_extrude(%s, %s)' % (profile, path)
        if profile and len(points) >= 2:
            var = v.get('var') or 'path_extrude_1'
            return '%s_path = spline([%s])\n%s_path%spath_extrude(%s, %s_path)' % (var, ', '.join(points), var, PRELUDE, profile, var)
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
        params = _parameters_of(name)
        parts = ([body] if body else []) + [o['name'] for o in others if o.get('type') in ('solid', 'profile', 'import', 'simulation', 'lattice')]
        args, missing = [], False
        for k in range(_parts_of(name)):
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
        if _is_flow(name):
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
    cell = _nice(longest / 10.0)
    if r.get('name') in _FOAMS or r.get('name') == 'random_points':
        # (a foam is made of the cells round points that do not repeat: a tenth of the block across would be a thousand points and a foam that takes minutes to draw -- a quarter or so of it
        # is a hundred or so, drawn in a few seconds, and the spacing is a number to change)
        cell = _nice(longest / 3.5)

    return {'es': _num(_nice(longest / 20.0)),
            # (a lattice's cells: a tenth of the model across, their members an eighth of that thick)
            'cs': _num(cell), 'ct': _num(_nice(longest / 10.0) / 8.0)}


def _stroke_text(name, places, smooth, r):
    ''' A profile, a polyline or a spline of the points `places` (3-tuples of numbers as texts): the points, each a model of its own with its gizmo, written above the call.  While
        the symmetry is on (`r['symmetry']`) the images of the points are made too, each following the point it is the image of (`symmetry_image`), and the shapes through them:
        the stroke itself and its images, those that meet where they cross a plane joined into one outline '''
    from fieldes import symmetry_creation as sym
    elements = sym.elements_of(r.get('symmetry'))
    points, listing = _points(*places)
    smooth_arg = ', smooth=True' if name == 'profile' and smooth else ''
    if not elements:
        return points + '#@@#\n' + '%s([%s]%s)' % (name, listing, smooth_arg)
    positions = [tuple(float(c) for c in p) for p in places]
    step = float((r.get('plane') or {}).get('step') or 0.0)
    tol = max(1e-4, 1e-3 * step)
    r['_symmetric'] = True
    symmetry_var = (r.get('symmetry') or {}).get('var') or 'symmetry_1'
    whole = sym.simple_images(positions, elements, tol)
    if whole is not None:
        # The images are strokes that touch nothing: each is the image of the stroke, like the image of any model, `{var}_m1 = symmetry_image({var}, symmetry_1, 1)`, written under the
        # stroke (so that whatever is done to the stroke -- a point dragged, pulled out of an edge, deleted -- is done to its images)
        text = points + '#@@#\n' + '%s([%s]%s)' % (name, listing, smooth_arg)
        for k, el in enumerate(whole, 1):
            text += (sym.POST if k == 1 else '') + sym.image_statement('{var}', '{var}_m%d' % k, el, symmetry_var)
        return text
    made, shapes = sym.stroke_structure(positions, elements, name, name == 'profile', tol)
    n = len(places)
    # (the image points, each held to the point it is the image of)
    images = ''
    symmetry_var = (r.get('symmetry') or {}).get('var') or 'symmetry_1'
    for i, e, k in made:
        images += sym.image_statement('{var}_p%d' % (i + 1), '{var}_p%d_m%d' % (i + 1, e + 1), elements[e], symmetry_var)

    def names(nodes):
        return ', '.join(sym.point_name('{var}', k, n, made) for k in nodes)

    def call(shape_name, nodes):
        if shape_name == 'profile' and smooth:
            return 'profile(\n    [%s],\n    smooth=True,\n)' % names(nodes)
        return '%s([%s])' % (shape_name, names(nodes))

    # (the first shape is the model itself; the others are each a shape of its own beside it, named `var_m1`, `var_m2` ...)
    extra = ''
    for k, (shape_name, nodes) in enumerate(shapes[1:], 1):
        extra += '{var}_m%d = %s\n{var}_m%d\n' % (k, call(shape_name, nodes), k)
    first_name, first_nodes = shapes[0]
    return points + images + extra + '#@@#\n' + call(first_name, first_nodes)


def _plane_template(name, r):
    ''' A profile or a line made on the work plane (`r['plane']`: origin, u, v, and the step of its grid, 0 for none): its points are laid out in the plane's own axes
        round the place of the click -- the corners of a square for a profile -- not in the horizontal plane, and on the grid when the plane snaps '''
    plane = r['plane']
    u = tuple(float(c) for c in plane['u'])
    v = tuple(float(c) for c in plane['v'])
    centre = tuple(float(r.get(k, 0.0)) for k in ('x', 'y', 'z'))
    h = _nice(float(r.get('scale', 10.0))) / 2.0
    step = float(plane.get('step') or 0.0)
    if step > 0:
        h = max(step, round(h / step) * step)               # (the points are on the grid too)

    def at(a, b):
        return tuple(_num(c + a * x + b * y) for c, x, y in zip(centre, u, v))

    places = [at(-h, -h), at(h, -h), at(h, h), at(-h, h)] if name == 'profile' else [at(-h, 0.0), at(0.0, h), at(h, 0.0)]
    return _stroke_text(name, places, False, r)


def _placed_template(name, r):
    ''' A profile, a polyline or a spline put down on the work plane: the points the user clicked, each a model of its own with its gizmo, and the call through them.
        A profile of a spline (`smooth`) has spline points '''
    places = [tuple(_num(float(c)) for c in p) for p in r['points']]
    return _stroke_text(name, places, r.get('smooth'), r)


def _symmetry_template(r, values):
    ''' A symmetry made from the menu: the mirror the viewport has -- `r['symdef']`: planes, kind, centre, axis, first (from the planes of a model, or the card) -- or a mirror
        at the place of the click: on the work plane while its view is open (its axis the plane's normal, its first plane along the plane's u), else the plane x = 0 through it '''
    d = r.get('symdef')
    if d:
        planes, kind, centre, axis, first = d['planes'], d['kind'], d['centre'], d['axis'], d['first']
    elif r.get('plane'):
        plane = r['plane']
        u, v = [float(c) for c in plane['u']], [float(c) for c in plane['v']]
        centre = [r.get('x', 0.0), r.get('y', 0.0), r.get('z', 0.0)]
        planes, kind = 1, 'axis'
        axis = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        first = u
    else:
        planes, kind = 1, 'axis'
        centre = [r.get('x', 0.0), r.get('y', 0.0), r.get('z', 0.0)]
        axis, first = [0, 0, 1], [0, 1, 0]

    def vec(v):
        return '(%s)' % ', '.join(_num(float(c)) for c in v)
    if kind == 'turn':
        return "symmetry_planes(copies=%d, kind='turn', centre=%s, axis=%s, first=%s)" % (int(planes), vec(centre), vec(axis), vec(first))
    return 'symmetry_planes(planes=%d, kind=%r, centre=%s, axis=%s, first=%s)' % (int(planes), kind, vec(centre), vec(axis), vec(first))


def _default_stroke_template(name, values, r):
    ''' The profile, polyline or spline of the menu with a symmetry on: the places of its points, from the template's own numbers '''
    if name == 'profile':
        places = [(values['xm'], values['ym'], values['z']), (values['xp'], values['ym'], values['z']), (values['xp'], values['yp'], values['z']),
                  (values['xm'], values['yp'], values['z'])]
    else:
        places = [(values['xm'], values['y'], values['z']), (values['x'], values['yp'], values['z']), (values['xp'], values['y'], values['z'])]
    return _stroke_text(name, places, False, r)


def _values(r):
    ''' What the templates are filled from: the place, the size, the models, and the boxes and numbers worked out from them '''
    name = r['name']
    s = _nice(float(r.get('scale', 10.0)))
    step = _step(s)

    if name == 'point':
        step = s / 1000.0                   # (a point is where the cursor is, to a thousandth of the span of a hundred pixels: not rounded to a grid of the view)

    def snap(v):
        return round(float(v) / step) * step

    if r.get('plane'):
        # (made on the work plane: the place is where its grid says, in the plane's axes -- rounding each of the world's coordinates would take it off the plane)
        x, y, z = (float(r.get(k, 0.0)) for k in ('x', 'y', 'z'))
    else:
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


def _images_after(r, head, expression):
    ''' The images a model made with the symmetry on has, written under it (see symmetry_creation): the models a menu entry places -- a shape, a point, a surface, a field -- and
        not what it is made of other models (an operation, a condition), a profile or a line (their points have the images: see _stroke_text) '''
    if r.get('_symmetric') or not (r.get('symmetry') or {}).get('elements') or not (r.get('symmetry') or {}).get('var'):
        return ''
    from fieldes import symmetry_creation as sym
    if r['kind'] == 'operation':
        return _operation_images(r)
    if r['kind'] != 'primitive':
        return ''
    for n, group, _ in PRIMITIVES:
        if n == r['name']:
            if group not in _PLACED_GROUPS or n in ('material', 'profile', 'polyline', 'spline', 'symmetry_planes'):
                return ''
            break
    else:
        return ''
    if r.get('body') or r.get('other') or r.get('points'):
        return ''
    text = sym.post_for(r.get('var') or (r['name'] + '_1'), expression, r, head)
    return sym.POST + text if text else ''


# The operations whose result is a body or a 2D shape of the model they are made for: with the symmetry on they are given their images too (`path_extrude(circle, curve)` is one
# tube; its images are the others)
_IMAGED_GROUPS = ('Offsets and walls', 'Texturing', 'Rounding', 'Moving', 'Repeating', 'Extruding', 'Combining')


def _operation_images(r):
    ''' The images of what an operation made with the symmetry on: `x_images = symmetry_images(x, symmetry, distinct=True)` -- the images that are not the model itself or another
        image (they cannot be told here, where the models are only names: the run tells them).  Not for what is made of the images of a model (they are the images already), nor
        for what is not a body or a 2D shape (a field, a surface, an analysis, a lattice) '''
    from fieldes import symmetry_creation as sym
    for n, group, _template, _other in OPERATIONS:
        if n == r['name']:
            if group not in _IMAGED_GROUPS or n == 'instance':
                return ''
            break
    else:
        return ''
    # (what is selected: the first model and the others -- a path_extrude is made of a 2D shape and a curve, whichever was selected first)
    models = [dict(r.get('body_info') or {}, name=r.get('body') or '')] + list(_others(r))
    if not any(m.get('type') in ('solid', 'profile') for m in models) or any(m.get('symmetry_image') for m in models):
        return ''
    return sym.POST + sym.images_line(r.get('var') or (r['name'] + '_1'), r['symmetry']['var'], ', distinct=True')


def call(request):
    ''' The text of the call a menu entry writes, complete and laid out (fieldes.completion: what the call needs and was not given is a
        placeholder, every other argument is written with its default, one argument to a line when it does not fit).  `request` is JSON:
        {kind: 'primitive' | 'operation', name, x, y, z (the place), scale (mm spanned by about a hundred pixels there), body, other (names
        or expressions of the models), var (the name the model will get), indent (how far the statement is indented) } '''
    r = json.loads(request) if isinstance(request, str) else request
    if r.get('kind') == 'python':
        # (a block of plain Python: its code as it is -- no model is made of it)
        for n, _label, _doc, code in PYTHON:
            if n == r['name']:
                return code
        raise ValueError(tr('the Python menu has no block called %s') % r['name'])
    if r.get('kind') == 'primitive':
        # (a primitive of the user's own: ONE call of the function in its file -- `name_1 = name()` -- and not the statements it is made of: see user_primitives)
        from fieldes import user_primitives
        mine = user_primitives.find(r['name'])
        if mine is not None:
            return '%s()' % mine['name']
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
    return head + marker + expression + _images_after(r, head, expression)


KEEPS_ITS_TEMPLATE = ('lattice', 'lattice_surface_conform', 'random_points', 'voronoi_lattice', 'delaunay_lattice', 'surface_lattice', 'extrude')

# (the shapes of the first groups are made where the user clicks, as big as the view shows: the place and the size are what the click says.
# Everything else a menu writes is made of models and calls: the numbers in them are for the user to say)
_PLACED_GROUPS = ('1D', '3D', '2D', 'Points', 'Surfaces', 'Fields', 'Symmetry', 'Materials', 'Fluids',
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
    if kind == 'primitive' and r.get('check') and not _primitive_fits(name, selected):
        # (asked whether the entry is valid for this selection, by a menu on a model: the entry is made of what is selected or has a use for it, or it is not valid)
        raise ValueError('%s does not fit what is selected' % name)
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
        # With several models selected the FIRST is the body the condition is on and the others say where on it: bodies that choose the place are written as `body=`, and the call makes
        # the surface of the body there itself (`fixed(plate, body=bracket)`).  Surfaces and fields that are selected with it are the place already
        body = None
        first = selected[0]
        if len(selected) >= 2 and first.get('type') in _BODY_TYPES:
            where = _where(selected[1:])
            if where:
                regions = where
                if any(o.get('type') in _BODY_TYPES for o in where) and name != 'heat_generation':
                    body = first['name']
        text = _region_arg(regions)
        return BODY_CONDITIONS[name].format(region=text + (', body=' + body if body else ''), volume=text, **values)
    if kind == 'operation' and (values['body'] or name in _SIMULATIONS) and (r.get('others') is not None or r.get('body_info')
                                                                             or name in _SIMULATIONS):
        built = _build_operation(name, values, others)
        if built:
            return built
    if kind == 'primitive' and r.get('points') and name in ('profile', 'polyline', 'spline'):
        r['_templated'] = True
        return _placed_template(name, r).format(**values)
    if kind == 'primitive' and name == 'symmetry_planes':
        r['_templated'] = True
        return _symmetry_template(r, values)
    if kind == 'primitive' and r.get('plane') and name in ('profile', 'polyline', 'spline'):
        r['_templated'] = True
        return _plane_template(name, r).format(**values)
    if kind == 'primitive':
        for n, _, template in PRIMITIVES:
            if n == name:
                r['_templated'] = True
                if name in ('profile', 'polyline', 'spline') and (r.get('symmetry') or {}).get('elements'):
                    return _default_stroke_template(name, values, r).format(**values)
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
