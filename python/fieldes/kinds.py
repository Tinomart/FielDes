'''
What a model is: its KIND.

Everything a script makes is one of a few kinds, and FielDes shows the kind with an icon of its own colour in the model tree
(and in the right-click menus that create them):

    solid       a 3D shape (a body: the field is negative inside it)
    curve       a 1D shape: a line or a spline through points (drawn as a line; as a field the distance to it)
    profile     a 2D shape (a field that does not depend on z: drawn flat, in the z = 0 plane)
    field       a field that is not a body: a distance, a ramp, noise, a stress ... (a value at every point of space)
    surface     an open surface (the zero set of a field, with no body behind it); a patch picked on a surface is one too
    point       a point
    simulation  a solved analysis or optimisation
    material    what a part (or a fluid) is made of: Material(...), steel, aluminium, water ...: an analysis is given one
    conditions  what an analysis is given: supports, loads, thermal and flow boundary conditions
    cell        what a lattice is made of: a cell (a strut cell, a TPMS, a planar pattern, a custom cell), a cell map,
                a graph of beams
    lattice     a body that is a lattice: what lattice(), lattice_surface_conform() ... make (a body filled with cells)
    points      a set of points: what random_points() makes, the points a foam is made around (drawn as the dots of gizmos)
    import      a file read in
    symmetry    mirror planes through a centre: what is made while it is selected is made with its images
    block       (a model made by one of your custom blocks carries a small block mark on its icon)

kind_of(value) says which kind a value is, or None for a value that is not a model (a number, a string, a list).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''

# kind: (the words for it, its colour)
KINDS = {
    'solid':      ('3D shape',    '#4aa8e8'),
    'curve':      ('1D shape',    '#c9d3e6'),
    'profile':    ('2D shape',    '#35c4b3'),
    'field':      ('Field',       '#82cc58'),
    'surface':    ('Surface',     '#b583ee'),
    'point':      ('Point',       '#f4b73a'),
    'simulation': ('Simulation',  '#ee6a5e'),
    'material':   ('Material',    '#c9a66b'),
    'conditions': ('Conditions',  '#e08f58'),
    'cell':       ('Lattice cell', '#d4b43c'),
    'lattice':    ('Lattice',     '#e0709c'),
    'points':     ('Point set',   '#f0a24c'),
    'import':     ('Import',      '#2aa198'),
    'symmetry':   ('Symmetry',    '#d33682'),
    'block':      ('Custom block', '#8da2c0'),
}

# The DOMAINS of engineering: what an analysis is about.  The Engineering block of the right-click menus has a category for each, and the model tree shows a
# simulation, and what an analysis is given, in the colour and icon of its domain: the result in the full colour, its conditions in a lighter tint of it.
# domain: (the words for it, the colour of a simulation, the colour of what it is given)
DOMAINS = {
    'structural': ('Structural', '#ee6a5e', '#f4958c'),
    'thermal':    ('Thermal',    '#f08c2a', '#f5b46e'),
    'fluid':      ('Fluid',      '#6f86f2', '#a1b0f7'),
}

# The domain of what a function makes (an analysis, a condition), by its name: the one list the menus, the model tree and the icons read it from
DOMAIN_OF_FUNCTION = {
    'static_analysis': 'structural', 'modal_analysis': 'structural', 'topology_optimization': 'structural',
    'fixed': 'structural', 'force': 'structural', 'gravity': 'structural', 'thermal_expansion': 'structural',
    'thermal_analysis': 'thermal', 'thermal_topology_optimization': 'thermal',
    'fixed_temperature': 'thermal', 'heat_input': 'thermal', 'heat_generation': 'thermal', 'convection': 'thermal',
    'fluid_analysis': 'fluid', 'flow_topology_optimization': 'fluid',
    'inlet': 'fluid', 'outlet': 'fluid', 'wall': 'fluid', 'slip': 'fluid',
}

# ... and of a value, by its class (a subclass is of its parent's domain)
_DOMAIN_OF_CLASS = {
    'Result': 'structural', 'ModalResult': 'structural', 'TopologyResult': 'structural',
    'ThermalResult': 'thermal', 'ThermalTopologyResult': 'thermal',
    'FluidResult': 'fluid', 'FlowTopologyResult': 'fluid',
    '_Support': 'structural', '_Force': 'structural', '_Gravity': 'structural', '_Thermal': 'structural',
    '_Temperature': 'thermal', '_Heat': 'thermal', '_HeatInput': 'thermal', '_HeatGeneration': 'thermal', '_Convection': 'thermal',
    '_Inlet': 'fluid', '_Outlet': 'fluid', '_Wall': 'fluid', '_Slip': 'fluid',
}


def domain_of(callee):
    ''' The domain of what the function `callee` (a name) makes, or '' '''
    return DOMAIN_OF_FUNCTION.get(callee or '', '')


def domain_of_value(value):
    ''' The domain of a simulation or of a condition (its class, or its parent's), or '' '''
    for cls in type(value).__mro__:
        found = _DOMAIN_OF_CLASS.get(cls.__name__)
        if found:
            return found
    return ''


# The classes of the library that are not shapes, by name (a subclass is of its parent's kind)
_BY_CLASS = {
    'Result': 'simulation', 'ModalResult': 'simulation', 'ThermalResult': 'simulation', 'FluidResult': 'simulation',
    'TopologyResult': 'simulation', 'FlowTopologyResult': 'simulation',
    'Material': 'material', 'Fluid': 'material', '_Support': 'conditions', '_Force': 'conditions',
    '_Gravity': 'conditions', '_Thermal': 'conditions', '_Temperature': 'conditions', '_Heat': 'conditions',
    '_HeatInput': 'conditions', '_HeatGeneration': 'conditions',
    '_Convection': 'conditions', '_Inlet': 'conditions', '_Outlet': 'conditions', '_Wall': 'conditions',
    '_Slip': 'conditions',
    'LatticeCell': 'cell', 'CellMap': 'cell', 'LatticeGraph': 'cell', 'PointSet': 'points',
    'SurfaceSelection': 'surface',
    'Regression': 'field', 'FieldFit': 'field',
}


def _axes(shape):
    ''' Which coordinates the shape's field depends on (bit 0 x, bit 1 y, bit 2 z), or 7 when that cannot be told '''
    try:
        from fieldes.ffi import lib
        fn = getattr(lib, 'libfive_tree_axes', None)
        return int(fn(shape.ptr)) if fn is not None else 7
    except Exception:
        return 7


def kind_of(value):
    ''' The kind of a model (see the top of this file): one of the keys of KINDS, or None for a value that is no model '''
    from fieldes.shape import Shape
    if isinstance(value, Shape):
        k = getattr(value, '_kind', None)
        if k in ('point', 'points', 'surface', 'field', 'profile', 'curve', 'lattice', 'symmetry'):
            return k
        if k == 'const':
            return 'field'
        for cls in type(value).__mro__:
            if cls.__name__ in _BY_CLASS:
                return _BY_CLASS[cls.__name__]
        axes = _axes(value)
        return 'profile' if axes and not axes & 4 else 'solid'
    for cls in type(value).__mro__:
        found = _BY_CLASS.get(cls.__name__)
        if found:
            return found
    return None


def keep_kind(out, shape):
    ''' `out` is `shape` edited in a way that does not change what it is (a gizmo, handles): a 2D shape is still one,
        although its field now depends on z the way the edit is written (a rotation about x) '''
    if kind_of(shape) in ('profile', 'lattice'):
        out._kind = kind_of(shape)


def describe():
    ''' The kinds as {kind: {label, color}}, for the model tree '''
    return {k: {'label': label, 'color': color} for k, (label, color) in KINDS.items()}


def describe_domains():
    ''' The domains as {domain: {label, color, conditions}}, for the model tree and the menus: the colour of a simulation and of what it is given '''
    return {k: {'label': label, 'color': color, 'conditions': tint} for k, (label, color, tint) in DOMAINS.items()}


# What a function can be GIVEN, as the kinds of model it takes.  A function says it of itself, next to its definition -- `fn._takes = takes(...)` -- and everything that asks (the
# right-click menus: what is valid for the models selected; the model tree: what can be dropped on a call) reads it from the function, so a new function is covered by one line
# on it and no list anywhere has its name.  A function that says nothing takes any shape.
SHAPES = frozenset(('solid', 'profile', 'curve', 'import', 'simulation', 'field', 'surface', 'point', 'lattice'))     # (what a geometric operation works on)
BODIES = frozenset(('solid', 'profile', 'lattice', 'import', 'simulation'))     # (what has an inside, and edges: not a surface, a curve, a point or a field)


def takes(accepts, needs=()):
    ''' The declaration of what a function takes: `accepts`, the kinds of model it can be given together, and `needs`, a list of sets of kinds -- the selection must hold at least one
        of each set.  `takes(BODIES | {'field'}, [BODIES])` is a body, and a field for the number if one is selected with it '''
    return [(frozenset(accepts), [frozenset(n) for n in needs])]
