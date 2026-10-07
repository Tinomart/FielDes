'''
What a model is: its KIND.

Everything a script makes is one of a few kinds, and FielDes shows the kind with an icon of its own colour in the model tree
(and in the right-click menus that create them):

    solid       a 3D shape (a body: the field is negative inside it)
    profile     a 2D shape (a field that does not depend on z: drawn flat, in the z = 0 plane)
    field       a field that is not a body: a distance, a ramp, noise, a stress ... (a value at every point of space)
    surface     an open surface (the zero set of a field, with no body behind it); a patch picked on a surface is one too
    point       a point
    simulation  a solved analysis or optimisation
    material    what a part (or a fluid) is made of: Material(...), steel, aluminium, water ...: an analysis is given one
    conditions  what an analysis is given: supports, loads, thermal and flow boundary conditions
    cell        what a lattice is made of: a cell, a cell map, a graph of beams
    import      a file read in
    block       (a model made by one of your custom blocks carries a small block mark on its icon)

kind_of(value) says which kind a value is, or None for a value that is not a model (a number, a string, a list).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''

# kind: (the words for it, its colour)
KINDS = {
    'solid':      ('3D shape',    '#4aa8e8'),
    'profile':    ('2D shape',    '#35c4b3'),
    'field':      ('Field',       '#82cc58'),
    'surface':    ('Surface',     '#b583ee'),
    'point':      ('Point',       '#f4b73a'),
    'simulation': ('Simulation',  '#ee6a5e'),
    'material':   ('Material',    '#c9a66b'),
    'conditions': ('Conditions',  '#e08f58'),
    'cell':       ('Lattice cell', '#d4b43c'),
    'import':     ('Import',      '#2aa198'),
    'block':      ('Custom block', '#8da2c0'),
}

# The classes of the library that are not shapes, by name (a subclass is of its parent's kind)
_BY_CLASS = {
    'Result': 'simulation', 'ModalResult': 'simulation', 'ThermalResult': 'simulation', 'FluidResult': 'simulation',
    'TopologyResult': 'simulation', 'FlowTopologyResult': 'simulation',
    'Material': 'material', 'Fluid': 'material', '_Support': 'conditions', '_Force': 'conditions',
    '_Gravity': 'conditions', '_Thermal': 'conditions', '_Temperature': 'conditions', '_Heat': 'conditions',
    '_Convection': 'conditions', '_Inlet': 'conditions', '_Outlet': 'conditions', '_Wall': 'conditions',
    '_Slip': 'conditions', '_PlainConditions': 'conditions', 'StaticBoundaryConditions': 'conditions',
    'LatticeCell': 'cell', 'CellMap': 'cell', 'LatticeGraph': 'cell',
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
        if k in ('point', 'surface', 'field', 'profile'):
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
    ''' `out` is `shape` edited in a way that does not change what it is (a gizmo, exposed numbers): a 2D shape is still one,
        although its field now depends on z the way the edit is written (a rotation about x) '''
    if kind_of(shape) == 'profile':
        out._kind = 'profile'


def describe():
    ''' The kinds as {kind: {label, color}}, for the model tree '''
    return {k: {'label': label, 'color': color} for k, (label, color) in KINDS.items()}
