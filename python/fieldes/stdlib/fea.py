'''
Static finite element analysis (linear elasticity) of FielDes shapes.

    from fieldes import *

    bracket = ...                                        # any Shape, in mm
    base = fixed(box((0, 0, 0), (5, 40, 20)))            # clamp the left end
    push = force(box((95, 0, 0), (100, 40, 20)), (0, 0, -200))   # 200 N down
    result = static_analysis(bracket, supports=[base], loads=[push], material=aluminium, element_size=1.0)
    base, push                                           # each is drawn on the part the analysis was given: held (blue), pushed (red)
    result                                   # the stress on the deformed part (FielDes: the result card)
    stiffer = bracket - 0.002 * result.von_mises   # results are fields like any other

Supports and loads are regions -- ordinary shapes: a support fixes the part
wherever it lies inside the region, a load spreads a total force over the
part's surface inside the region.  Units: mm, N and MPa (E in MPa), so
stresses come out in MPa and displacements in mm.

The elements are the `element` option of static_analysis, modal_analysis and
topology_optimization (thermal_analysis has it too):
    'tet'        linear tetrahedra that follow the part's surface (the
                 default): an unstructured mesh made for the part by isosurface
                 stuffing on the shape's own field -- its boundary vertices lie
                 on the surface, sharp edges are followed to a fraction of an
                 element -- with `element_size` the edge length, a stiffness
                 matrix assembled from each element's own geometry and a
                 preconditioned conjugate-gradient solve.  The stress in a
                 tetrahedron is constant in it; linear tetrahedra are stiff in
                 bending, so refine (a cantilever four elements thick reads 96 %
                 of beam theory, eight thick 99 %)
    'tet10'      the same tetrahedra with a node in the middle of every edge: 10-node
                 (quadratic) elements, a displacement that is a quadratic in each
                 element and a stress that is linear in it, with the edges on the
                 part's surface curved to follow it.  For static_analysis and
                 modal_analysis.  They do not bend stiff, and for a smooth part about
                 three times the element size of 'tet' reads the same stress -- but
                 on the same mesh they cost about 15 times the memory and the time,
                 so use them where the stress matters (a notch, a hole, a fillet)
                 and the part's thin features do not set the element size
    'hex'        a voxel grid of cubes, `element_size` on a side, each one
                 trilinear hexahedron with incompatible modes: bends without
                 spurious shear; its stiffness follows how much of the cube lies
                 inside the part, so the surface is a staircase
    'hex_basic'  the same grid with the plain trilinear hexahedron (shear-locks
                 in bending)
Topology optimization designs one density per element, on the tetrahedra or on
the grid; thermal topology optimization is on the grid only.  Smaller elements
are more accurate and slower; results are cached in memory, so re-running an
unchanged analysis (e.g. while editing other parts of a FielDes script) is
instant.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import math
import time
import weakref
from collections import OrderedDict

from fieldes.ffi import lib, libfive_region_t
from fieldes.i18n import tr as _tr
from fieldes.shape import Shape, field_shape
from fieldes.stdlib.excluded import keep_regions, carry_locks
from fieldes.stdlib.content_cache import Uncacheable, cache_for, problem_key, shape_key, value_key
from fieldes.stdlib import result_cache

# The element a cell of the voxel grid is made of (Element in fea.hpp)
ELEMENTS = {'tet': 0, 'hex': 1, 'hex_basic': 2}


def _set_element(ptr, element):
    if element not in ELEMENTS:
        raise ValueError("element is 'tet' (tetrahedra that follow the part, the default), 'tet10' (the same with 10 nodes: static and modal "
                         "analysis), 'hex' (voxel hexahedra with incompatible modes) or 'hex_basic' (plain hexahedra)")
    if getattr(lib, 'libfive_fea_set_element', None) is None:
        if element != 'hex':
            raise FeaError('this FielDes library is too old for the {!r} element'.format(element))
        return
    lib.libfive_fea_set_element(ptr, ELEMENTS[element])

inf = math.inf          # (`displacement=(0, 0, inf)`: see fixed)

__all__ = ['Material', 'steel', 'stainless_steel', 'aluminium', 'titanium',
           'pla', 'petg', 'abs_plastic', 'nylon',
           'fixed', 'inf', 'force', 'gravity', 'thermal_expansion', 'static_analysis', 'colored',
           'modal_analysis', 'ModalResult', 'Mode',
           'FeaError', 'topology_optimization', 'TopologyResult']


class FeaError(RuntimeError):
    ''' The analysis could not be set up or solved (the message says why) '''


def _property(value, what, positive=True):
    ''' A material property: a number, or a field (a Shape: its value at each point of the part) '''
    if isinstance(value, Shape):
        return value
    try:
        number = float(value)
    except (TypeError, ValueError):
        raise TypeError('Material: {} is a number or a field (a Shape), not {!r}'.format(what, type(value).__name__))
    if positive and not number > 0:
        raise ValueError('Material: {} must be positive'.format(what))
    return number


def _is_field(value):
    return isinstance(value, Shape)


def _number(value, default=0.0):
    ''' What a solver call that takes a number gets for a property that may be a field: the field is given by a call of its own,
        and this number is only what it is measured against '''
    return float(default) if isinstance(value, Shape) else float(value)


class Material:
    ''' An isotropic linear-elastic material: Young's modulus E (MPa),
        Poisson's ratio nu, density (t/mm^3, for gravity loads and the mass of a modal analysis),
        thermal conductivity (W / (mm K), for thermal_analysis), thermal expansion
        coefficient (1/K, for thermal_expansion).

        E, density, conductivity and expansion can each be a FIELD instead of a number -- a Shape, its value at every
        point of the part is the property there: `Material('graded', ramp(z_field(), (0, 50), (70e3, 3e3)), 0.33, 2.7e-9)`
        is stiff at the bottom and soft at the top; a lattice's density field can drive it as well.  (Fields in a material
        and in loads work with the tetrahedral elements, the default.)  nu and yield_strength are numbers. '''
    def __init__(self, name, E, nu, density=0.0, yield_strength=None, conductivity=None,
                 expansion=None):
        self.name = name
        self.E = _property(E, 'E')
        if isinstance(nu, Shape):
            raise TypeError("Material: nu (Poisson's ratio) is a number, not a field")
        self.nu = float(nu)
        self.density = _property(density, 'density', positive=False)
        if isinstance(yield_strength, Shape):
            raise TypeError('Material: yield_strength is a number (the safety factor is one number)')
        self.yield_strength = yield_strength
        self.conductivity = conductivity if conductivity is None else _property(conductivity, 'conductivity')
        self.expansion = expansion if expansion is None else _property(expansion, 'expansion', positive=False)

    def __repr__(self):
        return 'Material({!r}, E={}, nu={:g})'.format(
            self.name, 'a field' if _is_field(self.E) else '{:g} MPa'.format(self.E), self.nu)


# (conductivity in W / (mm K): 1 W / (m K) = 1e-3 W / (mm K); expansion:
# typical linear coefficients, 1/K)
steel = Material('steel', 200e3, 0.30, 7.85e-9, 250, 0.050, 12e-6)
stainless_steel = Material('stainless steel', 193e3, 0.29, 8.0e-9, 215, 0.016, 17e-6)
aluminium = Material('aluminium', 69e3, 0.33, 2.70e-9, 95, 0.167, 23e-6)
titanium = Material('titanium (Ti-6Al-4V)', 114e3, 0.34, 4.43e-9, 880, 0.0067, 8.6e-6)
pla = Material('PLA', 3.5e3, 0.36, 1.24e-9, 50, 0.00013, 68e-6)
petg = Material('PETG', 2.1e3, 0.38, 1.27e-9, 45, 0.00020, 60e-6)
abs_plastic = Material('ABS', 2.2e3, 0.35, 1.04e-9, 40, 0.00017, 90e-6)
nylon = Material('nylon (PA12)', 1.7e3, 0.39, 1.01e-9, 45, 0.00024, 100e-6)


_PARTS = weakref.WeakKeyDictionary()      # (condition -> the bodies the simulations that have it were given: kept out of the condition, which is hashed by what it holds)


class _Condition:
    ''' What every boundary condition is -- a support, a load, a temperature, an inlet ...: a model of its own (a row of the model tree with an
        eye), that shows itself the way it acts: the surface it acts on, tinted, with the pads or arrows and what it says (see
        boundary_conditions).  It is drawn on the body of the simulation it was given to, or -- while no simulation has it yet -- on the body
        its surfaces were picked on.  It has no body in it: the simulation is given the body '''

    _no_gizmo = True                # (not something to drag: FielDes hides the handles button)

    def __repr__(self):
        # (what the result line shows when a condition is the last thing a script made: what it is, not where it is in memory)
        try:
            from fieldes.stdlib.boundary_conditions import describe
            return describe(self)
        except Exception:
            return '<%s>' % type(self).__name__

    def _used_for(self, part):
        ''' A simulation was given this condition for `part`: it is drawn on it '''
        parts = _PARTS.setdefault(self, [])
        if isinstance(part, Shape) and not any(p is part for p in parts):
            parts.append(part)

    def _display(self):
        from fieldes.stdlib.boundary_conditions import display_condition
        return display_condition(self)


class _Support(_Condition):
    _call = 'fixed(...)'

    def __init__(self, region, displacement):
        self.region = region
        # (dx, dy, dz) in mm: where the part is held in each direction -- 0 does not let it move, `inf` leaves the direction free, another number moves the part there by that much)
        self.displacement = displacement
        self.axes = tuple(not math.isinf(d) for d in displacement)          # (the directions that are held)


def _support_args(s):
    ''' What the kernel is given for a support: (x, y, z), 1 for a held direction, then the displacement (mm) the held directions are held at '''
    return tuple(int(a) for a in s.axes) + tuple(float(d) if held else 0.0 for held, d in zip(s.axes, s.displacement))


def _support_flags(s, what):
    ''' The held directions of a support, for a solver that cannot impose a displacement (the voxel one): a support that holds at another number than 0 is refused '''
    if any(held and d != 0 for held, d in zip(s.axes, s.displacement)):
        raise FeaError("{}: fixed(..., displacement=...) imposes a displacement only on tetrahedra (element='tet' or 'tet10'): this analysis holds a part still or leaves it free".format(what))
    return tuple(int(a) for a in s.axes)


class _Force(_Condition):
    _call = 'force(...)'

    def __init__(self, region, vector, profile=None):
        self.region, self.vector, self.profile = region, vector, profile


class _Gravity(_Condition):
    _call = 'gravity(...)'

    def __init__(self, g):
        self.g = g


class _Thermal(_Condition):
    _call = 'thermal_expansion(...)'

    def __init__(self, temperature, reference):
        self.temperature, self.reference = temperature, reference


def _shape(x, what):
    x = _field_of(x)
    if not isinstance(x, Shape):
        raise TypeError('{} must be a Shape (a region), not {!r}'.format(what, type(x).__name__))
    return x


def _field_of(x):
    ''' A result of an analysis or an optimisation is a field like any other: where a shape goes, a result goes, and it is its field there '''
    if not isinstance(x, Shape) and getattr(type(x), '_as_field', None) is not None:
        return x._as_field()
    return x


class FieldResult:
    ''' What every result is -- of an analysis or of an optimisation: a FIELD, like any shape.  Wherever a shape goes (`offset(result, 1)`,
        `difference(part, result)`, `result * 2`, the part of the next analysis) the result goes, and it is its own field there:

            an optimisation       the final design (the shape it made)
            static_analysis       the von Mises stress          modal_analysis      the first mode's displacement
            thermal_analysis      the temperature               fluid_analysis      the speed

        and `result.fields` lists every field it calculated -- `{name: field}` -- the others being attributes by those names (`result.pressure`).
        Whatever the result is made visible as -- the optimisation's iterations, the flow's streamlines -- is how it is shown when it is
        stated on its own: the field it is passed as does not change that. '''

    def _as_field(self):
        raise TypeError('{} has no field of its own'.format(type(self).__name__))

    @property
    def fields(self):
        ''' Every field the result calculated, as {name: field} '''
        return OrderedDict()

    def __getattr__(self, name):
        # (a name the result does not have is a name of its field: `result.min(...)`, `result.sqrt()` -- never the private ones)
        if name.startswith('_') or self.__dict__.get('_resolving'):
            raise AttributeError(name)
        self.__dict__['_resolving'] = True
        try:
            field = self._as_field()
        except Exception:
            raise AttributeError(name) from None
        finally:
            self.__dict__['_resolving'] = False
        return getattr(field, name)


def _delegate(name):
    def op(self, *args):
        return getattr(self._as_field(), name)(*args)
    op.__name__ = name
    return op


for _name in ('__add__', '__radd__', '__sub__', '__rsub__', '__mul__', '__rmul__', '__truediv__', '__rtruediv__', '__pow__', '__neg__',
              '__abs__'):
    setattr(FieldResult, _name, _delegate(_name))


# The conditions an analysis is given, one input for each kind (the name of the input is the name of the kind: a force goes in `loads`).  Every
# input is needed -- the call is written with a placeholder for each -- and takes one item, or a list of them (an empty list says there is none).
# Each module that makes conditions says its kinds: slot name -> (the classes, what they are written as)
_SLOTS = {}


def _register_slots(slots):
    _SLOTS.update(slots)


_register_slots({'supports': ((_Support,), 'fixed(...) items'),
                 'loads': ((_Force, _Gravity, _Thermal), 'force(...), gravity(...) and thermal_expansion(...) items')})


def _slot_of(item):
    ''' The input a condition goes in (`supports`, `loads`, `inlets` ...), or None '''
    for name, (classes, _) in _SLOTS.items():
        if isinstance(item, classes):
            return name
    return None


def _given(value, slot, what):
    ''' The conditions given to the input `slot` of the analysis `what`, as a list: one item, or a list (a list in a list is the same list) '''
    classes, text = _SLOTS[slot]
    out = []

    def add(v):
        if isinstance(v, (list, tuple)):
            for x in v:
                add(x)
        elif isinstance(v, classes):
            out.append(v)
        else:
            where = _slot_of(v)
            raise TypeError('{what}: {slot} are {text}, and {got} is not one{where}'.format(
                what=what, slot=slot, text=text, got='a ' + getattr(v, '_call', type(v).__name__) if where else repr(v)[:40],
                where=' -- it goes in %s=' % where if where else ''))

    add(value)
    return out


def _with_conditions(fn):
    ''' An analysis that has an input for each kind of condition.  A call that leaves some out says which, and why: the form there was before gave
        all of them as one list, which is not an input any more -- each kind has its own, so that every kind is in front of you '''
    import functools
    import inspect
    sig = inspect.signature(fn)

    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        # (a result is a field: the part of an analysis -- or its body, its domain -- may be the result of another)
        args = tuple(_field_of(a) for a in args[:2]) + tuple(args[2:])
        for key in ('body', 'domain', 'part', 'shape'):
            if key in kwargs:
                kwargs[key] = _field_of(kwargs[key])
        old = [k for k in ('walls', 'slips') if k in kwargs and 'boundaries' in sig.parameters]
        if old:
            raise TypeError('{fn}: {old} {is_} not an input any more -- the walls and the slips are one input, boundaries, that takes wall(...) and slip(...) '
                            'items, one kind or both, in one list: boundaries=[wall(...), slip(...)]'.format(
                                fn=fn.__name__, old=' and '.join(old), is_='is' if len(old) == 1 else 'are'))
        if 'max_iterations' in kwargs:
            raise TypeError('{fn}: max_iterations is not an argument any more -- the equation solvers stop by themselves when the residual is below `tolerance` '
                            '(and say so when they cannot get there), so a limit was never something to set: remove it from the call'.format(fn=fn.__name__))
        try:
            sig.bind(*args, **kwargs)
        except TypeError:
            try:
                got = sig.bind_partial(*args, **kwargs).arguments
            except TypeError:
                raise
            if 'domain' in sig.parameters and 'domain' not in got:
                raise TypeError('{fn}: the flow goes around a body inside a domain -- {fn}(body, domain, inlets, outlets, boundaries, ...), the inputs of '
                                'topology_optimization of a flow too: the fluid is the domain with the body cut out, and the force is the force on the body. '
                                '{fn} was given only the fluid before; now give the body, then the domain that holds it.'.format(fn=fn.__name__)) from None
            kinds = [n for n in sig.parameters if n in _SLOTS]
            missing = [n for n in kinds if sig.parameters[n].default is sig.parameters[n].empty and n not in got]
            if not missing:
                raise
            first = next(iter(sig.parameters))
            raise TypeError(
                '{fn}: every kind of boundary condition is an input of its own, and {missing} {was} not given -- give each kind its own list ([] for none): '
                '{fn}({first}, {example}, ...). One list that holds conditions of several kinds is not an input any more.'.format(
                    fn=fn.__name__, missing=', '.join(missing), was='was' if len(missing) == 1 else 'were', first=first,
                    example=', '.join('%s=[...]' % n for n in kinds))) from None
        result = fn(*args, **kwargs)
        # (how the result was asked for: parametric_optimization asks again with other numbers in the var()s of the model)
        try:
            result._recipe = (wrapper, args, kwargs)
        except AttributeError:
            pass
        return result
    return wrapper


def _use_conditions(items, part):
    ''' The simulation was given `part` with these conditions: they are drawn on it '''
    for c in items:
        use = getattr(c, '_used_for', None)
        if use is not None:
            use(part)


def _united(regions):
    ''' Several regions as one: the places where any of them is (the smallest of their fields), in the box all of them lie in '''
    out = regions[0]
    for r in regions[1:]:
        out = out.min(r)
    if len(regions) > 1:
        out._members = list(regions)            # (what the one region is made of: the body a surface of it was picked on is found there)
    boxes = [getattr(r, '_bounds', None) for r in regions]
    if len(regions) > 1 and all(boxes):
        out._bounds = (tuple(min(b[0][i] for b in boxes) for i in range(3)), tuple(max(b[1][i] for b in boxes) for i in range(3)))
    return out


def _regions(args, region, what, body=None):
    ''' The regions a condition is given -- ANY NUMBER of them, bodies, fields or selected surfaces, one argument after the other (a list of
        them is the same) -- as one region, and the arguments that follow them: (region, rest).  `region=` is the same as one of them.
        `body=`: the regions are where the condition acts ON THAT BODY: the region is its surface where they are (`surface_from_bodies(body, regions)`), made here --
        a plate that a part sits against is the region, the part is the body, and the condition acts on the part's surface where it meets the plate '''
    found = []
    for r in (region if isinstance(region, (list, tuple)) else [region]):
        if r is not None:
            found.append(_shape(r, what + ' region'))
    i = 0
    while i < len(args):
        a = args[i]
        if isinstance(a, Shape):
            found.append(a)
        elif isinstance(a, (list, tuple)) and a and all(isinstance(x, Shape) for x in a):
            found.extend(a)
        else:
            break
        i += 1
    if not found:
        raise TypeError('{} needs the region it acts on first -- a body, a field or a selected surface; any number of them, '
                        'one argument after the other'.format(what))
    if body is not None:
        from fieldes.stdlib.selection import surface_from_bodies
        return surface_from_bodies(_shape(body, what + ' body'), *found), tuple(args[i:])
    return _united(found), tuple(args[i:])


def fixed(*regions, region=None, body=None, displacement=(0.0, 0.0, 0.0), **removed):
    ''' A support: the part is held wherever it lies inside `region` (a Shape: a body, a field, a selected surface).  Any number
        of regions can be given, one argument after the other -- fixed(a, b) holds the part where either is.

        region        where the part is held: a body, a field or a selected surface (the same as the first argument)
        body          the body the support is on, when the regions are bodies that choose WHERE on it: `fixed(plate, body=bracket)` holds the bracket on its surface where it
                      meets the plate (what `surface_from_bodies(bracket, plate)` is, made by the call)
        displacement  (dx, dy, dz) in mm, where the part is held in each direction -- also written after the regions, fixed(region, (0, 0, inf)).
                      0 (the default, (0, 0, 0)) lets the part not move there: it is fixed.  `inf` (`math.inf`, `float('inf')`: `from fieldes import *` has it as
                      `inf`) leaves that direction free, so `displacement=(0, 0, inf)` is a support the part slides along z on.  Any other number is a
                      displacement the support imposes: `(0, 0, -0.5)` pushes the part 0.5 mm down where it is held (a static analysis on tetrahedra
                      solves for the rest of it; a modal analysis holds it still, a topology optimization refuses it). '''
    if removed:
        raise TypeError("fixed(): {} {} gone -- a direction is left free by an infinite displacement: fixed(region, displacement=(0, 0, inf)) lets the part slide along z".format(
            ', '.join(k + '=' for k in sorted(removed)), 'is' if len(removed) == 1 else 'are'))
    found, rest = _regions(regions, region, 'fixed()', body)
    if rest:
        if displacement != (0.0, 0.0, 0.0):
            raise TypeError('fixed(): the displacement is given once -- as the argument after the regions, or as displacement=')
        displacement = rest[0] if len(rest) == 1 else rest
    try:
        d = tuple(displacement)
    except TypeError:
        raise TypeError('fixed(): displacement is three numbers (dx, dy, dz), mm: 0 holds the part, inf leaves the direction free') from None
    if len(d) != 3 or any(isinstance(v, (bool, Shape)) or not isinstance(v, (int, float)) or math.isnan(v) for v in d):
        raise TypeError('fixed(): displacement is three numbers (dx, dy, dz), mm: 0 holds the part, inf leaves the direction free (True / False flags for the directions are gone)')
    return _Support(found, tuple(float(v) for v in d))

fixed._required = ('region',)


def force(*args, region=None, body=None, vector=None, profile=None):
    ''' A load: the total force (fx, fy, fz) in N, spread evenly over the
        part's surface inside `region` (a Shape).  force(region, (0, 0, -100)) and
        force(region, 0, 0, -100) and force(region=region, vector=(0, 0, -100)) are the same.  Any number of regions can be given, one
        argument after the other, before the force: force(a, b, (0, 0, -100)) loads the part where either is.

        region   where the part is loaded: a body, a field or a selected surface (the same as the first argument)
        body     the body the load is on, when the regions are bodies that choose WHERE on it: `force(pad, (0, 0, -100), body=bracket)` loads the bracket's surface where it meets the pad
        vector   the total force (fx, fy, fz) in newtons: its direction and its size
        profile  a field: the total is spread over the surface in proportion to it (not negative) instead of evenly --
                 a pressure that is not the same everywhere, e.g. `profile=ramp(x_field(), (0, 80), (0.2, 1.0))` loads
                 the end of a beam five times harder at x = 80 than at x = 0, with the same total.  (Tetrahedral
                 elements.) '''
    found, rest = _regions(args, region, 'force()', body)
    if vector is not None:
        if rest:
            raise TypeError('force(): the force is given once -- as the argument after the regions, or as vector=')
        rest = (vector,)
    if len(rest) == 1 and isinstance(rest[0], (list, tuple)):
        rest = tuple(rest[0])
    if len(rest) != 3:
        raise TypeError('force(region, ..., (fx, fy, fz)): the force is three numbers (N), after the regions it acts on')
    fx, fy, fz = rest
    if any(isinstance(v, Shape) for v in (fx, fy, fz)):
        raise TypeError('force(): the components of a force are numbers (N); to spread it unevenly over the region give '
                        'profile=<a field>')
    if profile is not None and not isinstance(profile, Shape):
        raise TypeError('force(): profile is a field (a Shape)')
    return _Force(found, (float(fx), float(fy), float(fz)), profile)

force._required = ('region', 'vector')


def gravity(g=(0.0, 0.0, -9810.0)):
    ''' The part's own weight: acceleration in mm/s^2 (default: 1 g down
        along -Z), with the material's density '''
    return _Gravity(tuple(float(v) for v in g))


def thermal_expansion(temperature, reference=20.0):
    ''' A load from heat: the part at `temperature` -- a field (e.g. a
        thermal analysis' .temperature) or a number -- expands by the
        material's expansion coefficient per degree above `reference`
        (where it is stress-free).  Held parts are stressed; free ones grow.
        Static analysis only. '''
    if not isinstance(temperature, Shape):
        temperature = Shape(lib.libfive_tree_const(float(temperature)))
    return _Thermal(temperature, float(reference))


def _density_given(material):
    return isinstance(material.density, Shape) or material.density > 0


def _fields_in_use(material, loads=()):
    ''' The names of the fields a material and loads are made with (the voxel elements take numbers only) '''
    used = [a for a in ('E', 'density', 'conductivity', 'expansion') if isinstance(getattr(material, a, None), Shape)]
    if any(getattr(l, 'profile', None) is not None for l in loads if isinstance(l, _Force)):
        used.append('a load profile')
    return used


def _refuse_fields(material, loads, element, what):
    if element not in ('tet', 'tet10'):
        used = _fields_in_use(material, loads)
        if used:
            raise FeaError("{}: {} {} a field -- fields in a material and in loads work with the tetrahedral elements "
                           "(element='tet', the default, or 'tet10'), not {!r}".format(what, ', '.join(used),
                                                                           'is' if len(used) == 1 else 'are', element))


def _material_fields(ptr, material):
    ''' The material's fields on a tetrahedral problem (its numbers went into the calls that take them) '''
    for attr, name in (('E', 'stiffness'), ('density', 'density'), ('expansion', 'expansion')):
        value = getattr(material, attr, None)
        if isinstance(value, Shape):
            fn = getattr(lib, 'libfive_tetfea_set_%s_field' % name, None)
            if fn is None:
                raise FeaError('this FielDes library is too old for a field as a material property')
            fn(ptr, value.ptr)


def _add_force(ptr, l, case=None):
    ''' A force on a tetrahedral problem, with its profile if it has one '''
    if l.profile is not None:
        fn = getattr(lib, 'libfive_tetfea_add_force_profile', None)
        if fn is None:
            raise FeaError('this FielDes library is too old for a load profile')
        fn(ptr, l.region.ptr, *l.vector, 0 if case is None else case, l.profile.ptr)
    elif case is None:
        lib.libfive_tetfea_add_force(ptr, l.region.ptr, *l.vector)
    else:
        lib.libfive_tetfea_add_force_case(ptr, l.region.ptr, *l.vector, case)


# FielDes draws at most this many elements' values a field (more are not drawn)
from fieldes.stdlib.element_mesh import MAX_DRAWN as _MAX_DRAWN_VALUES, element_mesh as _element_mesh, elements_note as _elements_note

_FIELDS = ['von_mises', 'displacement', 'ux', 'uy', 'uz',
           'sxx', 'syy', 'szz', 'sxy', 'syz', 'szx',
           'max_principal', 'min_principal', 'strain_energy']
_LABELS = {'von_mises': _tr('von Mises stress (MPa)'), 'displacement': _tr('displacement (mm)'),
           'ux': _tr('displacement x (mm)'), 'uy': _tr('displacement y (mm)'), 'uz': _tr('displacement z (mm)'),
           'sxx': _tr('stress xx (MPa)'), 'syy': _tr('stress yy (MPa)'), 'szz': _tr('stress zz (MPa)'),
           'sxy': _tr('stress xy (MPa)'), 'syz': _tr('stress yz (MPa)'), 'szx': _tr('stress zx (MPa)'),
           'max_principal': _tr('max principal stress (MPa)'),
           'min_principal': _tr('min principal stress (MPa)'),
           'strain_energy': _tr('strain energy density (mJ/mm^3)')}


class _Handle:
    ''' Owns a libfive_fea object (freed with the last result using it) '''
    def __init__(self, ptr):
        self.ptr = ptr

    def __del__(self):
        if self.ptr:
            lib.libfive_fea_delete(self.ptr)
            self.ptr = None


class Result(FieldResult):
    ''' The solved analysis.  Fields (Shapes whose value is the result at
        each point, usable in any expression): von_mises, displacement,
        ux, uy, uz, sxx .. szx, max_principal, min_principal,
        strain_energy.  Summary: max_von_mises, max_displacement,
        safety_factor (if the material has a yield strength), elements,
        iterations, seconds, reaction (support force), ... '''

    def __init__(self, handle, shape, material, element_size, element='tet'):
        self._handle = handle
        self.shape = shape
        self.material = material
        self.element_size = element_size
        self.element = element
        p = handle.ptr
        self._ranges = {}
        for i, name in enumerate(_FIELDS):
            ptr = lib.libfive_fea_field(p, i)
            setattr(self, name, field_shape(ptr))
            self._ranges[name] = (lib.libfive_fea_field_min(p, i), lib.libfive_fea_field_max(p, i))
        stat = lambda k: lib.libfive_fea_stat(p, k)
        # (the solver counts cells of the grid; a cell of tetrahedra is six elements)
        self.cells = int(stat(0))
        self.elements = self.cells * (6 if element == 'tet' else 1)
        self.nodes = int(stat(1))
        self.unknowns = int(stat(2))
        self.iterations = int(stat(3))
        self.residual = stat(4)
        self.seconds = stat(5)
        self.volume = stat(6)
        self.compliance = stat(7)
        self.reaction = (stat(8), stat(9), stat(10))
        self.total_load = (stat(11), stat(12), stat(13))
        self.grid = (int(stat(16)), int(stat(17)), int(stat(18)))
        # material no support holds (a loose piece): left out of the analysis
        self.loose_elements = int(stat(19))
        lower = (ctypes.c_double * 3)()
        h = ctypes.c_double()
        dims = (ctypes.c_int * 3)()
        self._grid = None
        if lib.libfive_fea_grid(p, lower, ctypes.byref(h), dims):
            n = dims[0] * dims[1] * dims[2]
            frac = (ctypes.c_float * n)()
            lib.libfive_fea_elements(p, frac)
            # (corner, element size, element counts, fill fractions as float32 bytes)
            self._grid = (tuple(lower), h.value, tuple(dims), bytes(frac))
        # The peak stress is the elements' own (the smooth field averages the elements at
        # each node, which lowers a peak), so a safety factor made from it is not optimistic
        self.max_von_mises_smoothed = self._ranges['von_mises'][1]
        self.max_von_mises = self.max_von_mises_smoothed
        self._element_ranges = {}
        rng = getattr(lib, 'libfive_fea_element_range', None)
        if rng is not None:
            for i, name in enumerate(_FIELDS):
                lo_, hi_ = ctypes.c_float(), ctypes.c_float()
                if rng(p, i, ctypes.byref(lo_), ctypes.byref(hi_)):
                    self._element_ranges[name] = (lo_.value, hi_.value)
            if 'von_mises' in self._element_ranges:
                self.max_von_mises = self._element_ranges['von_mises'][1]
        self.max_displacement = self._ranges['displacement'][1]
        ys = material.yield_strength
        self.safety_factor = (ys / self.max_von_mises) if ys and self.max_von_mises > 0 else None

    def _as_field(self):
        return self.von_mises

    @property
    def fields(self):
        return OrderedDict((name, getattr(self, name)) for name in _FIELDS)

    @property
    def mass(self):
        ''' The part's mass in kg: the volume of its mesh times the material's density (None when the density is a field) '''
        density = self.material.density
        if isinstance(density, Shape):
            return None
        return self.volume * density * 1000.0

    def range(self, field='von_mises'):
        ''' (min, max) of a field over the part: of the smooth field (the nodal values;
            stresses are averages of the elements at each node) '''
        return self._ranges[field]

    def element_range(self, field='von_mises'):
        ''' (min, max) of a field over the elements, each with its own value (a tetrahedron's
            stress is constant in it; a hexahedron's is taken at its centre).  A stress peak
            is higher here than in range(), which averages the elements at each node. '''
        return self._element_ranges.get(field, self._ranges[field])

    def _display(self):
        ''' What FielDes shows for the result stated on its own: the part coloured by the von Mises stress and
            deformed (the largest movement 5 % of the part's size).  The result card switches the field, scales
            the deformation, shows the elements, and steps the load from 5 % to 100 % (slider, play / pause):
            the stresses and the deformation grow in proportion, as a linear analysis does. '''
        shown = getattr(self, '_shown', None)
        if shown is None:
            size = self._model_size()
            auto = 0.05 * size / self.max_displacement if self.max_displacement > 0 else 1.0
            steps = []
            for k in range(1, 21):
                f = k / 20.0
                if k == 20:
                    steps.append({'label': _tr('load 100 %')})
                else:
                    steps.append({'label': _tr('load %d %%') % (5 * k),
                                  'channels': [(name, _LABELS[name], f * getattr(self, name)) + tuple(self._ranges[name])
                                               for name in _FIELDS],
                                  'deform': (f * self.ux, f * self.uy, f * self.uz)})
            shown = {'_color_fields': [(name, _LABELS[name], getattr(self, name)) + tuple(self._ranges[name])
                                       for name in _FIELDS],
                     '_color_field_name': 'von_mises', '_deform': (self.ux, self.uy, self.uz),
                     '_deform_auto': auto, '_deform_scale': auto,
                     '_fea_grid': self._element_grid(), '_fea_element_text': self.element_text(),
                     '_color_steps': steps, '_color_step': len(steps) - 1}
            self._shown = shown
        out = colored(self.shape, self.von_mises, range=self._ranges['von_mises'], label=_LABELS['von_mises'])
        out.__dict__.update(shown)
        out._color_detail = float(getattr(self, 'element_size', 0) or 0)
        return out

    def element_text(self):
        ''' What the analysis is made of, and how many of them '''
        if self.element == 'tet':
            return _tr('%s tetrahedra') % '{:,}'.format(self.elements)
        return _tr('%s hexahedra') % '{:,}'.format(self.elements)

    def _element_grid(self):
        ''' What FielDes draws for "Elements": (corner, cell size, cell counts, fill fractions,
            element, [each field's value in every element, float32 bytes]).  The values are the
            elements' own (a tetrahedron's stress is constant in it), read from the solver, not
            the smooth nodal fields.  None when the analysis has more elements than are drawn. '''
        if self._grid is None:
            return None
        cached = getattr(self, '_grid_full', False)
        if cached is not False:
            return cached
        full = None
        fn = getattr(lib, 'libfive_fea_element_values', None)
        if fn is not None:
            p = self._handle.ptr
            n = int(fn(p, 0, None, 0))
            if 0 < n <= _MAX_DRAWN_VALUES:
                values = []
                for i in range(len(_FIELDS)):
                    buf = (ctypes.c_float * n)()
                    fn(p, i, buf, n)
                    values.append(bytes(buf))
                full = self._grid + (self.element, values)
        self._grid_full = full
        return full

    def _model_size(self):
        ''' The part's extent (for the automatic deformation scale) '''
        return max(self.grid) * self.element_size

    def deformed(self, scale=None, field_shape=None):
        ''' The part moved by the computed displacements, magnified by
            `scale` (default: so the largest movement is 5% of the part's
            size).  Pass field_shape to deform another shape the same way. '''
        s = field_shape if field_shape is not None else self.shape
        if scale is None:
            size = self._model_size()
            scale = 0.05 * size / self.max_displacement if self.max_displacement > 0 else 1.0
        x, y, z = Shape.X(), Shape.Y(), Shape.Z()
        return s.remap(x - scale * self.ux, y - scale * self.uy, z - scale * self.uz)

    def __repr__(self):
        text = _tr('Static analysis: %s (%.1f s)\n  von Mises max %.4g MPa \u00b7 displacement max %.3g mm') % (
            self.element_text(), self.seconds, self.max_von_mises, self.max_displacement)
        if self.safety_factor is not None:
            text += '\n' + _tr('  safety factor %.3g (%s)') % (self.safety_factor, self.material.name)
        if self.loose_elements:
            text += '\n' + _tr('  %d loose elements (connected to no support) left out') % self.loose_elements
        return text


_cache = OrderedDict()
# Whole problems by their content (see problem_key): an analysis asked again is not built again at all
# (the caches above and below are keyed by the prepared mesh, which means meshing it first)
_early = OrderedDict()


def _bounds(shape):
    b = getattr(shape, '_bounds', None)
    if b:
        return b
    # (the extent of a shape is found by searching it: remembered by the shape's expression)
    memory = cache_for('bounds', 64)
    try:
        key = shape_key(shape)
    except Uncacheable:
        key = None
    if key is not None:
        hit, found = memory.get(key)
        if hit:
            return found
    from fieldes.stdlib.fields import find_extent       # (searched with the script's var() numbers)
    found = find_extent(shape)
    if found is None:
        raise FeaError('could not find the extent of the part: pass bounds=(xyz_min, xyz_max)')
    if key is not None:
        memory.put(key, found)
    return found


@_with_conditions
def static_analysis(shape, supports, loads, material=steel, element_size=None,
                    bounds=None, tolerance=1e-6, cache=True, element='tet'):
    ''' Linear static analysis of `shape` (a Shape, in mm).

        supports   what holds the part: fixed(...) items (at least one), one or a list
        loads      what pushes it: force(...) / gravity(...) / thermal_expansion(...) items, one or a list ([] for none).
                   The conditions have no body in them (this function is given it); each is a model of its own and is drawn on the part
        material   a Material (default steel); E in MPa
        element_size   the element edge in mm (default: the part's longest
                   side over 60; over 30 with element='tet10')
        element    'tet' (default: tetrahedra that follow the part's surface),
                   'tet10' (the same with 10 nodes: quadratic elements, about
                   three times the element size for the same accuracy),
                   'hex' or 'hex_basic' (a voxel grid): see the module
                   documentation
        bounds     region to analyse, ((x0, y0, z0), (x1, y1, z1)); found
                   automatically if not given

        Returns a Result (see its fields).  Raises FeaError if the problem
        can't be solved as given (no support touching the part, supports
        that don't hold it in place, ...). '''
    max_iterations = 20000      # (the equation solver's own limit, never reached by a problem that has a solution: not an argument -- it stops by itself when the residual is below `tolerance`)
    if not isinstance(shape, Shape):
        raise TypeError('static_analysis: the part must be a Shape')
    if _is_cases(loads):
        raise FeaError('a static analysis has one set of loads: load cases (a list of lists) are for '
                       'topology_optimization')
    supports = _given(supports, 'supports', 'static_analysis')
    loads = _given(loads, 'loads', 'static_analysis')
    _use_conditions(supports + loads, shape)
    # The whole problem by its content, before anything is built: a problem asked again (a script run
    # again, a section moving over its fields) is neither meshed nor solved again
    ekey = problem_key('static', shape=shape, part_bounds=getattr(shape, '_bounds', None),
                       supports=supports, loads=loads, material=material, element_size=element_size,
                       bounds=bounds, max_iterations=max_iterations, tolerance=tolerance,
                       element=element) if cache else None
    if ekey is not None and ekey in _early:
        _early.move_to_end(ekey)
        found = _early[ekey]
        return found if found.shape is shape else _copy_for(found, shape)
    result = _static_analysis_solve(shape, supports, loads, material, element_size, bounds,
                                    max_iterations, tolerance, cache, element, ekey)
    if ekey is not None:
        _early[ekey] = result
        while len(_early) > 8:
            _early.popitem(last=False)
    return result


def _static_analysis_solve(shape, supports, loads, material, element_size, bounds, max_iterations,
                           tolerance, cache, element, ekey=None):
    lo, hi = bounds if bounds is not None else _bounds(shape)
    size = [hi[i] - lo[i] for i in range(3)]
    if element_size is None:
        element_size = max(size) / (30.0 if element == 'tet10' else 60.0)
    element_size = float(element_size)
    if not element_size > 0:
        raise FeaError('element_size must be positive')
    # The grid starts exactly at the part's bounds: faces lying there (and
    # supports / loads placed on them) then coincide with element faces and
    # nodes instead of cutting elements in half, whose stiffness, smeared
    # over the whole cube, over-stiffened bending (a cantilever read ~15 %
    # too stiff from that alone)
    pad = 1e-6 * max(max(size), 1e-9)
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i] - pad, hi[i] + pad
    if element in ('tet', 'tet10'):
        return _static_analysis_tet(shape, supports, loads, material, element_size, region, max(size),
                                    max_iterations, tolerance, cache, ekey, element)

    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load('fea', ekey, 'static analysis')
    if loaded is not None:
        return Result(_Handle(loaded[0]), shape, material, element_size, element)
    _refuse_fields(material, loads, element, 'static_analysis')
    ptr = lib.libfive_fea_new(shape.ptr, region, element_size, material.E, material.nu)
    handle = _Handle(ptr)
    _set_element(ptr, element)
    for s in supports:
        if not isinstance(s, _Support):
            raise TypeError('supports must be fixed(...) items')
        lib.libfive_fea_add_support(ptr, s.region.ptr, *_support_flags(s, 'static_analysis'))
    for l in loads:
        if isinstance(l, _Force):
            lib.libfive_fea_add_force(ptr, l.region.ptr, *l.vector)
        elif isinstance(l, _Gravity):
            if not material.density > 0:
                raise FeaError('gravity needs a material with a density')
            lib.libfive_fea_set_gravity(ptr, *l.g, material.density)
        elif isinstance(l, _Thermal):
            if not getattr(material, 'expansion', None):
                raise FeaError('thermal_expansion needs a material with an expansion coefficient')
            lib.libfive_fea_set_thermal(ptr, l.temperature.ptr, float(material.expansion), l.reference)
        else:
            raise TypeError('loads must be force(...), gravity(...) or thermal_expansion(...) items')
    if not lib.libfive_fea_prepare(ptr):
        raise FeaError(lib.libfive_fea_message(ptr).decode('utf-8', 'replace'))

    key = lib.libfive_fea_hash(ptr)
    if cache and key in _cache:
        result = _cache[key]
        _cache.move_to_end(key)
        if result.shape is not shape:
            # Same problem from an identical part: same results, this part
            result = _copy_for(result, shape)
        return result
    if ekey is not None:
        lib.libfive_fea_set_salt(ptr, result_cache.salt('fea', ekey))
    if not lib.libfive_fea_solve(ptr, int(max_iterations), float(tolerance)):
        raise FeaError(lib.libfive_fea_message(ptr).decode('utf-8', 'replace'))
    result = Result(handle, shape, material, element_size, element)
    result_cache.save('fea', ekey, ptr, 'static analysis')
    if cache:
        _cache[key] = result
        while len(_cache) > 8:
            _cache.popitem(last=False)
    return result


class _TetHandle:
    ''' Owns a libfive_tetfea object (freed with the last result using it) '''
    def __init__(self, ptr):
        self.ptr = ptr

    def __del__(self):
        if self.ptr:
            lib.libfive_tetfea_delete(self.ptr)
            self.ptr = None


class TetResult(Result):
    ''' A static analysis on a body-fitted tetrahedral mesh: its elements are tetrahedra
        that follow the part's surface (not a voxelization), the stress in each is its
        own (constant in it; with element='tet10' linear in it), and the fields are read
        anywhere in the mesh.  Everything of Result: fields as shapes, .range(),
        .element_range(), ...  With the 10-node element the fields are on its nodes, and
        are drawn on the element cut into eight linear tetrahedra. '''

    def __init__(self, handle, shape, material, element_size, size, element='tet'):
        self._handle = handle
        self.shape = shape
        self.material = material
        self.element_size = element_size
        self.element = element
        self._size = float(size)
        p = handle.ptr
        self._ranges = {}
        for i, name in enumerate(_FIELDS):
            setattr(self, name, field_shape(lib.libfive_tetfea_field(p, i)))
            self._ranges[name] = (lib.libfive_tetfea_field_min(p, i), lib.libfive_tetfea_field_max(p, i))
        stat = lambda k: lib.libfive_tetfea_stat(p, k)
        self.elements = int(stat(0))
        self.cells = 0
        self.nodes = int(stat(1))
        self.unknowns = int(stat(2))
        self.iterations = int(stat(3))
        self.residual = stat(4)
        self.seconds = stat(5)
        self.volume = stat(6)
        self.compliance = stat(7)
        self.reaction = (stat(8), stat(9), stat(10))
        self.total_load = (stat(11), stat(12), stat(13))
        self.grid = (0, 0, 0)
        self.loose_elements = int(stat(19))
        self._grid = None
        self._element_ranges = {}
        for i, name in enumerate(_FIELDS):
            lo_, hi_ = ctypes.c_float(), ctypes.c_float()
            if lib.libfive_tetfea_element_range(p, i, ctypes.byref(lo_), ctypes.byref(hi_)):
                self._element_ranges[name] = (lo_.value, hi_.value)
        self.max_von_mises_smoothed = self._ranges['von_mises'][1]
        # (a 10-node element's stress is linear in it and highest at a node: the peak is read at the nodes, as their elements average it)
        self.max_von_mises = (self._ranges['von_mises'] if element == 'tet10' else
                              self._element_ranges.get('von_mises', self._ranges['von_mises']))[1]
        self.max_displacement = self._ranges['displacement'][1]
        ys = material.yield_strength
        self.safety_factor = (ys / self.max_von_mises) if ys and self.max_von_mises > 0 else None

    def _model_size(self):
        return self._size

    def element_text(self):
        if self.element == 'tet10':
            return _tr('%s 10-node tetrahedra \u00b7 %s nodes') % ('{:,}'.format(self.elements), '{:,}'.format(self.nodes))
        return _tr('%s tetrahedra \u00b7 %s nodes') % ('{:,}'.format(self.elements), '{:,}'.format(self.nodes))

    def _element_grid(self):
        return None

    def _mesh_data(self):
        ''' What FielDes draws for "Elements": (vertices, tetrahedra, boundary triangles, the
            tetrahedron of each, text, [each field's value in every tetrahedron], the
            displacement at every vertex) as float32 / int32 bytes -- the mesh the solver
            used and each element's own values.  None when there are too many elements. '''
        cached = getattr(self, '_mesh_full', False)
        if cached is not False:
            return cached
        p = self._handle.ptr
        counts = (ctypes.c_int64 * 3)()
        lib.libfive_tetfea_counts(p, counts)
        nv, nt, nf = counts[0], counts[1], counts[2]
        full = None
        if 0 < nt <= _MAX_DRAWN_VALUES:
            verts = (ctypes.c_float * (3 * nv))()
            tets = (ctypes.c_int32 * (4 * nt))()
            faces = (ctypes.c_int32 * (3 * nf))()
            face_tet = (ctypes.c_int32 * nf)()
            lib.libfive_tetfea_mesh(p, verts, tets, faces, face_tet)
            disp = (ctypes.c_float * (3 * nv))()
            lib.libfive_tetfea_node_displacements(p, disp)
            values = []
            for i in range(len(_FIELDS)):
                buf = (ctypes.c_float * nt)()
                lib.libfive_tetfea_element_values(p, i, buf, nt)
                values.append(bytes(buf))
            full = (bytes(verts), bytes(tets), bytes(faces), bytes(face_tet), self.element_text(), values, bytes(disp))
        self._mesh_full = full
        return full

    def _display(self):
        out = Result._display(self)
        out._fea_mesh = self._mesh_data()
        if out._fea_mesh is None:
            out._fea_elements_note = _elements_note(int(self.elements))      # (more tetrahedra than are drawn: the card says why it has no elements)
        return out


def _remember_conditions(result, supports, loads):
    ''' The supports and loads a static analysis was solved with, kept on its result: export_fea() writes them into a model file '''
    result._conditions = (list(supports), list(loads))
    return result


def _static_analysis_tet(shape, supports, loads, material, element_size, region, size, max_iterations,
                         tolerance, cache, ekey=None, element='tet'):
    ''' static_analysis on a body-fitted tetrahedral mesh (element 'tet10': with 10-node elements) '''
    if getattr(lib, 'libfive_tetfea_new', None) is None:
        raise FeaError("this FielDes library is too old for tetrahedral meshing")
    if element == 'tet10' and getattr(lib, 'libfive_tetfea_set_order', None) is None:
        raise FeaError("this FielDes library is too old for the 10-node element")
    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load('tetfea', ekey, 'static analysis')
    if loaded is not None:
        return _remember_conditions(TetResult(_TetHandle(loaded[0]), shape, material, element_size, size, element), supports, loads)
    # (a Young's modulus that is a field is measured against 1 MPa: each element has its own value of it)
    ptr = lib.libfive_tetfea_new(shape.ptr, region, element_size, _number(material.E, 1.0), material.nu)
    handle = _TetHandle(ptr)
    if element == 'tet10':
        lib.libfive_tetfea_set_order(ptr, 2)
    _material_fields(ptr, material)
    for s in supports:
        if not isinstance(s, _Support):
            raise TypeError('supports must be fixed(...) items')
        lib.libfive_tetfea_add_support(ptr, s.region.ptr, *_support_args(s))
    for l in loads:
        if isinstance(l, _Force):
            _add_force(ptr, l)
        elif isinstance(l, _Gravity):
            if not _density_given(material):
                raise FeaError('gravity needs a material with a density')
            lib.libfive_tetfea_set_gravity(ptr, *l.g, _number(material.density))
        elif isinstance(l, _Thermal):
            if not (isinstance(material.expansion, Shape) or getattr(material, 'expansion', None)):
                raise FeaError('thermal_expansion needs a material with an expansion coefficient')
            lib.libfive_tetfea_set_thermal(ptr, l.temperature.ptr, _number(material.expansion), l.reference)
        else:
            raise TypeError('loads must be force(...), gravity(...) or thermal_expansion(...) items')
    if not lib.libfive_tetfea_prepare(ptr):
        raise FeaError(lib.libfive_tetfea_message(ptr).decode('utf-8', 'replace'))
    key = (element, lib.libfive_tetfea_hash(ptr))
    if cache and key in _cache:
        result = _cache[key]
        _cache.move_to_end(key)
        if result.shape is not shape:
            result = _copy_for(result, shape)
        return result
    if ekey is not None:
        lib.libfive_tetfea_set_salt(ptr, result_cache.salt('tetfea', ekey))
    if not lib.libfive_tetfea_solve(ptr, int(max_iterations), float(tolerance)):
        raise FeaError(lib.libfive_tetfea_message(ptr).decode('utf-8', 'replace'))
    result = _remember_conditions(TetResult(handle, shape, material, element_size, size, element), supports, loads)
    result_cache.save('tetfea', ekey, ptr, 'static analysis')
    if cache:
        _cache[key] = result
        while len(_cache) > 8:
            _cache.popitem(last=False)
    return result


################################################################################
# Modal analysis

class Mode:
    ''' One natural mode of vibration: .frequency (Hz), and its shape as
        fields (Shapes usable in any expression): displacement, ux, uy, uz
        -- scaled so the largest movement is 1 (a shape, not an amplitude).
        Stated on its own, FielDes shows the part coloured by it and deformed by it, and the
        result card plays the vibration (the deformation through a cycle). '''

    def __init__(self, handle, index, frequency, shape, element_size, grid):
        self._handle = handle
        self.index = index
        self.frequency = frequency
        self.shape = shape
        self.element_size = element_size
        self._grid = grid
        p = handle.ptr
        self._ranges = {}
        for name, f in (('displacement', 1), ('ux', 2), ('uy', 3), ('uz', 4)):
            setattr(self, name, field_shape(lib.libfive_fea_mode_field(p, index, f)))
            self._ranges[name] = (lib.libfive_fea_mode_field_min(p, index, f),
                                  lib.libfive_fea_mode_field_max(p, index, f))

    def _model_size(self):
        return max(self._grid) * self.element_size

    def _display(self):
        ''' What FielDes shows for the mode stated on its own: the part coloured by the mode shape and
            deformed by it (the largest movement 5 % of the part's size).  The result card steps the
            vibration through a cycle (24 phases; play to see it vibrate). '''
        shown = getattr(self, '_shown', None)
        if shown is None:
            n = 24
            steps = []
            for k in range(n):
                s = math.sin(2 * math.pi * k / n)
                steps.append({'label': _tr('phase %d\u00b0') % (360 * k // n),
                              'deform': (s * self.ux, s * self.uy, s * self.uz)})
            auto = 0.05 * self._model_size()
            shown = {'_color_fields': [(name, _tr('mode %d %s') % (self.index + 1, name), getattr(self, name)) +
                                       tuple(self._ranges[name]) for name in ('displacement', 'ux', 'uy', 'uz')],
                     '_color_field_name': 'displacement', '_deform': (self.ux, self.uy, self.uz),
                     '_deform_auto': auto, '_deform_scale': auto,
                     '_color_steps': steps, '_color_step': n // 4, '_fea_mesh': self._mesh_data(),
                     '_fea_elements_note': getattr(self, '_elements_note', '')}
            self._shown = shown
        label = _tr('mode %d, %.4g Hz (displacement)') % (self.index + 1, self.frequency)
        out = colored(self.shape, self.displacement, range=self._ranges['displacement'], label=label)
        out.__dict__.update(shown)
        out._color_detail = float(getattr(self, 'element_size', 0) or 0)
        return out

    def _mesh_data(self):
        ''' What FielDes draws for "Elements" of a mode: only a mode on tetrahedra has them (see TetMode) '''
        return None

    def __repr__(self):
        return '<Mode %d: %.5g Hz>' % (self.index + 1, self.frequency)


class TetMode(Mode):
    ''' One natural mode of a part on a body-fitted tetrahedral mesh (see Mode) '''

    def __init__(self, handle, index, frequency, shape, size, element_size=0):
        self._handle = handle
        self.index = index
        self.frequency = frequency
        self.shape = shape
        self.element_size = element_size
        self._size = float(size)
        p = handle.ptr
        self._ranges = {}
        for name, f in (('displacement', 1), ('ux', 2), ('uy', 3), ('uz', 4)):
            setattr(self, name, field_shape(lib.libfive_tetfea_mode_field(p, index, f)))
            self._ranges[name] = (lib.libfive_tetfea_mode_field_min(p, index, f),
                                  lib.libfive_tetfea_mode_field_max(p, index, f))

    def _model_size(self):
        return self._size

    def _mesh_data(self):
        ''' What FielDes draws for "Elements" of the mode: the mesh the solver used and, in each tetrahedron, the mode's displacement at its centre (the fields in the order of the card:
            displacement, ux, uy, uz), with the mode's displacement at every vertex '''
        cached = getattr(self, '_mesh_full', False)
        if cached is not False:
            return cached
        full = None
        p, i = self._handle.ptr, self.index
        if hasattr(lib, 'libfive_tetfea_mode_element_values'):
            full = _element_mesh(lambda b: lib.libfive_tetfea_counts(p, b), lambda v, t, f, ft: lib.libfive_tetfea_mesh(p, v, t, f, ft),
                                 lambda f, buf, n: lib.libfive_tetfea_mode_element_values(p, i, f, buf, n), (1, 2, 3, 4), None,
                                 lambda buf: lib.libfive_tetfea_mode_node_displacements(p, i, buf))
            if full is None:
                counts = (ctypes.c_int64 * 3)()
                lib.libfive_tetfea_counts(p, counts)
                self._elements_note = _elements_note(int(counts[1]))
        self._mesh_full = full
        return full


class ModalResult(FieldResult):
    ''' The result of modal_analysis(): .frequencies (Hz, lowest first),
        .modes (a Mode each: its shape as fields), .seconds.  Stated on its
        own it shows the first mode (result.modes[1] the second, ...). '''

    def __init__(self, handle, shape, material, element_size, seconds, size=None):
        p = handle.ptr
        self.material = material
        self.seconds = seconds
        if size is not None:
            # (on a tetrahedral mesh)
            self.volume = lib.libfive_tetfea_mesh_volume(p) if hasattr(lib, 'libfive_tetfea_mesh_volume') else 0.0
            n = lib.libfive_tetfea_mode_count(p)
            self.modes = [TetMode(handle, i, lib.libfive_tetfea_mode_frequency(p, i), shape, size, element_size)
                          for i in range(n)]
        else:
            self.volume = lib.libfive_fea_stat(p, 6)
            grid = (int(lib.libfive_fea_stat(p, 16)), int(lib.libfive_fea_stat(p, 17)),
                    int(lib.libfive_fea_stat(p, 18)))
            n = lib.libfive_fea_mode_count(p)
            self.modes = [Mode(handle, i, lib.libfive_fea_mode_frequency(p, i), shape, element_size, grid)
                          for i in range(n)]
        self.frequencies = [m.frequency for m in self.modes]

    def __getitem__(self, i):
        return self.modes[i]

    @property
    def mass(self):
        ''' The part's mass in kg: the volume of its mesh times the material's density (None when the density is a field) '''
        density = self.material.density
        if isinstance(density, Shape):
            return None
        return self.volume * density * 1000.0

    def _as_field(self):
        mode = self.modes[0]
        for name in ('displacement', 'shape'):
            f = getattr(mode, name, None)
            if isinstance(f, Shape):
                return f
        raise TypeError('modal_analysis: the first mode has no displacement field')

    @property
    def fields(self):
        out = OrderedDict()
        for k, mode in enumerate(self.modes):
            f = getattr(mode, 'displacement', None)
            if isinstance(f, Shape):
                out['mode_%d' % (k + 1)] = f
        return out

    def __len__(self):
        return len(self.modes)

    def _display(self):
        return self.modes[0]._display()

    def __repr__(self):
        return _tr('Modal analysis: %s Hz (%s, %.1f s)') % (
            ' \u00b7 '.join('%.4g' % f for f in self.frequencies), self.material.name, self.seconds)


_modal_cache = OrderedDict()


@_with_conditions
def modal_analysis(shape, supports, material=steel, modes=6, element_size=None, bounds=None,
                   tolerance=1e-6, cache=True, element='tet'):
    ''' The natural frequencies and mode shapes of `shape` (a Shape, in
        mm), held by `supports` (fixed(...) items, one or a list) -- how it
        vibrates.  No loads are needed; the
        material's E and density are used.

        modes        how many (the lowest first)
        element_size mm (default: about 40 elements along the longest side;
                     20 with element='tet10')
        element      'tet' (default), 'tet10' (10-node elements: the frequencies of a
                     part that bends are far better on the same mesh), 'hex' or
                     'hex_basic': see the module documentation

        Returns a ModalResult: .frequencies (Hz), .modes[i] (the shape as
        fields -- displacement, ux, uy, uz).  The shapes are fields like any
        other: e.g. stiffen the part where the first mode moves most.  An
        unchanged problem is cached. '''
    max_iterations = 100        # (the equation solver's own limit, never reached by a problem that has a solution: not an argument -- it stops by itself when the residual is below `tolerance`)
    if not isinstance(shape, Shape):
        raise TypeError('modal_analysis: the part must be a Shape')
    if not _density_given(material):
        raise FeaError('modal_analysis needs a material with a density')
    _refuse_fields(material, (), element, 'modal_analysis')
    supports = _given(supports, 'supports', 'modal_analysis')
    _use_conditions(supports, shape)
    if not supports:
        raise FeaError('modal_analysis needs supports (fixed(...))')
    # The whole problem by its content, before anything is built (see static_analysis)
    key = problem_key('modal', shape=shape, part_bounds=getattr(shape, '_bounds', None), supports=supports,
                      material=material, modes=int(modes), element_size=element_size, bounds=bounds,
                      max_iterations=max_iterations, tolerance=tolerance,
                      element=element) if cache else None
    if key is not None and key in _modal_cache:
        _modal_cache.move_to_end(key)
        return _modal_cache[key]
    lo, hi = bounds if bounds is not None else _bounds(shape)
    size = [hi[i] - lo[i] for i in range(3)]
    if element_size is None:
        element_size = max(size) / (20.0 if element == 'tet10' else 40.0)
    element_size = float(element_size)
    pad = 1e-6 * max(max(size), 1e-9)
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i] - pad, hi[i] + pad
    tet = element in ('tet', 'tet10')
    if tet and getattr(lib, 'libfive_tetfea_new', None) is None:
        raise FeaError("this FielDes library is too old for tetrahedral meshing")
    if element == 'tet10' and getattr(lib, 'libfive_tetfea_set_order', None) is None:
        raise FeaError("this FielDes library is too old for the 10-node element")
    kind = 'tetfea' if tet else 'fea'
    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load(kind, key, 'modal analysis')
    if loaded is not None:
        handle = _TetHandle(loaded[0]) if tet else _Handle(loaded[0])
        result = ModalResult(handle, shape, material, element_size, float(loaded[1].get('seconds', 0)),
                             size=max(size) if tet else None)
        if key is not None:
            _modal_cache[key] = result
        return result
    if tet:
        ptr = lib.libfive_tetfea_new(shape.ptr, region, element_size, _number(material.E, 1.0), material.nu)
        handle = _TetHandle(ptr)
        if element == 'tet10':
            lib.libfive_tetfea_set_order(ptr, 2)
        _material_fields(ptr, material)
    else:
        ptr = lib.libfive_fea_new(shape.ptr, region, element_size, material.E, material.nu)
        handle = _Handle(ptr)
        _set_element(ptr, element)
    for s in supports:
        if not isinstance(s, _Support):
            raise TypeError('supports must be fixed(...) items')
        # (a vibration has no use for a displacement: the part is held still where a support holds it)
        (lib.libfive_tetfea_add_support if tet else lib.libfive_fea_add_support)(
            ptr, s.region.ptr, *(_support_args(s)[:3] + (0.0, 0.0, 0.0) if tet else _support_args(s)[:3]))
    if key is not None:
        (lib.libfive_tetfea_set_salt if tet else lib.libfive_fea_set_salt)(ptr, result_cache.salt(kind, key))
    t0 = time.time()
    if tet:
        if not lib.libfive_tetfea_modal(ptr, int(modes), _number(material.density), int(max_iterations),
                                        float(tolerance)):
            raise FeaError(lib.libfive_tetfea_message(ptr).decode('utf-8', 'replace'))
        result = ModalResult(handle, shape, material, element_size, time.time() - t0, size=max(size))
    else:
        if not lib.libfive_fea_modal(ptr, int(modes), float(material.density), int(max_iterations),
                                     float(tolerance)):
            raise FeaError(lib.libfive_fea_message(ptr).decode('utf-8', 'replace'))
        result = ModalResult(handle, shape, material, element_size, time.time() - t0)
    result_cache.save(kind, key, ptr, 'modal analysis', {'seconds': result.seconds})
    if key is not None:
        _modal_cache[key] = result
        while len(_modal_cache) > 4:
            _modal_cache.popitem(last=False)
    return result


def _copy_for(result, shape):
    r = object.__new__(type(result))
    r.__dict__.update(result.__dict__)
    r.shape = shape
    return r


class _ColoredShape(Shape):
    ''' A Shape displayed coloured by a field (see colored) '''


def colored(shape, field, range=None, label=None, colormap='turbo'):
    ''' `shape`, displayed in FielDes coloured by `field` (another Shape,
        e.g. an FEA result) with a colour map; hovering the model shows the
        value.  range=(lo, hi) fixes the colour scale (default: the field's
        range on the part).  Anywhere else it is simply `shape`. '''
    if not isinstance(shape, Shape) or not isinstance(field, Shape):
        raise TypeError('colored(shape, field): both must be Shapes')
    out = _ColoredShape(lib.libfive_tree_copy(shape.ptr))
    out._color_field = field
    out._color_range = tuple(float(v) for v in range) if range is not None else None
    out._color_label = label or ''
    out._color_map = colormap
    b = getattr(shape, '_bounds', None)
    if b:
        out._bounds = b
    # (the same geometry: a part drawn from its own triangles, or meshed at its own resolution, is drawn so coloured too)
    for name in ('_display_mesh', '_render_hint', '_display_field'):
        v = getattr(shape, name, None)
        if v is not None:
            setattr(out, name, v)
    if getattr(shape, '_display_field', None) is not None:
        out._distance_of = lambda: shape          # (what is meshed is the shape's own field; the distance a field viewer shows is the shape's)
    return out


################################################################################
# Topology optimization

class TopologyResult(FieldResult):
    ''' The result of topology_optimization():
        .density     a field, 0 (no material) .. 1 (solid), in the part
        .shape()     the optimized part, keeping the volume fraction asked
                     for (shape(threshold=0.5): where density > 0.5)
        .history     the objective at each iteration (what was made best: the compliance, the mass ...)
        .densities   the density field after each iteration (tetrahedral
                     optimizations)
        .volume_fraction, .iterations, .seconds
        .pieces      how many separate pieces the optimized part is in (tetrahedral optimizations; more than
                     one is warned about when the result is made: try a higher volume_fraction)
        .symmetry    the planes the design was kept symmetric about: {'z': 0.0} (see the symmetry argument), {} if none
        .analysis    the analysis of the optimized part, made as the one the optimisation was given was (a list with several): its numbers
                     -- `best.max_von_mises`, `best.mass` -- are the true ones; .values what was asked, as that part has it; .feasible
        .verify()    the analyses of the optimized part again (a list, one per analysis, when there are several)
        Stated on its own, FielDes shows the optimized part coloured by the
        density, and the result card steps through the iterations: the part
        as it was after each one (slider, play / pause). '''

    def __init__(self, handle, part, density, history, settings, element_size, bounds,
                 supports, loads, material, stats):
        self._handle = handle
        self.part = part
        self.density = density
        self.history = history
        self.settings = settings
        self.element_size = element_size
        self.bounds = bounds
        self._supports, self._loads, self.material = supports, loads, material
        self.volume_fraction = stats['volume_fraction']
        self.iterations = stats['iterations']
        self.seconds = stats['seconds']
        self.domain = part                      # (what the design may fill, as a field: the part; where it may grow, see topology_optimization)
        self.grow = False

    def keep_threshold(self):
        ''' The density above which the part has the volume the design has.  (The density field is partly grey -- between 0 and 1 -- so cutting at
            0.5 kept only 9-15 % of a block asked to keep 25 %: a much weaker part than the optimised one.)  The volume above a level is counted in
            the kernel, exactly: the field is linear in each tetrahedron.  A grid of samples cannot see a wall thinner than its spacing, and cut a
            thin bracket that was to keep half of its material at 38 % '''
        if getattr(self, '_keep_threshold', None) is not None:
            return self._keep_threshold
        ptr = getattr(getattr(self, '_handle', None), 'ptr', None)
        t = float(lib.libfive_tetfea_cut_threshold(ptr)) if ptr else 0.5
        self._keep_threshold = t
        return t

    def _cut_field(self, density, threshold):
        ''' The part cut from a density field at a level: where the field is above it -- scaled to roughly mm near the level so that offsets behave
            (the density rises from 0 to 1 over about two filter widths) -- within what the design may fill (`domain`): the part, or where the
            design may grow what it may grow into.  The density goes on beyond its mesh as it is at the mesh's edge, as every result field does, so
            the part has a face of its own wherever the design is bounded -- the part's, the clear space round a support or a load, the limit '''
        width = 2.0 * (self.settings['filter_radius'] or 1.5 * self.element_size)
        out = (threshold - density) * width
        return out if self.domain is None else out.max(self.domain)

    def _join(self, count, ptr, margin):
        ''' The number of pieces the optimised part is in.  The cut that keeps the volume asked for can leave it in pieces (the density between
            the cut and the next piece is only a little less): the cut is then lowered, step by step, to the first density at which the part is
            one piece -- a little more material, and a part that is a part.  When no cut does, the first one stays and the pieces are counted '''
        first = t = self.keep_threshold()
        n = int(count(ptr, t, margin))
        while n > 1 and t > 0.06:
            t = max(0.05, t - max(0.01, 0.08 * t))
            n = int(count(ptr, t, margin))
        if n > 1:
            self._keep_threshold = first
            return int(count(ptr, first, margin))
        if t != first:
            self._cut = (first, t)
            self._keep_threshold = t
        return n

    def shape(self, threshold=None):
        ''' The optimized part: the original part where the density is above
            `threshold` (a smooth surface through the density field).  By
            default the threshold that keeps the volume fraction asked for
            (see keep_threshold); 0.5 cuts the density field in the middle. '''
        if threshold is None:
            threshold = self.keep_threshold()
        out = self._cut_field(self.density, threshold)
        out._bounds = self.bounds
        return carry_locks(out, self.part)

    def verify(self, threshold=None, element_size=None):
        ''' The analyses of the optimized part again, each made as the one the optimisation was given was (a different element size if asked) --
            a list, one per analysis, when there are several '''
        shape = self.shape(threshold)
        out = []
        for a in self._analyses:
            args = dict(a.arguments)
            args['shape'] = shape
            args['element_size'] = element_size or args.get('element_size') or self.element_size
            args['bounds'] = self.bounds
            out.append(a.call(**args))
        return out if len(out) > 1 else out[0]

    def _display(self):
        ''' What FielDes shows for the result stated on its own: the optimized part (see shape())
            coloured by the density, with a step per iteration -- the part as it was then '''
        shown = getattr(self, '_shown', None)
        if shown is None:
            threshold = self.keep_threshold()
            densities = getattr(self, 'densities', None) or []
            steps = []
            # (a tetrahedral optimisation can say each step's surface itself, from its own densities: the viewer draws those triangles
            # instead of meshing the field of every step.  The field stays what sections and probes read.  A design that grew was found in
            # several design spaces, one after the other: each step says which one it is of, and which of its iterations)
            sources = getattr(self, '_step_sources', None)
            if sources is None:
                handle = getattr(self, '_handle', None)
                ptr = ctypes.cast(handle.ptr, ctypes.c_void_p).value if isinstance(handle, _TetHandle) and handle.ptr else None
                sources = [(ptr, k) for k in range(len(densities))]
            drawn = bool(getattr(lib, 'libfive_tetfea_density_at', None))
            for k, d in enumerate(densities):
                step = {'label': _tr('iteration %d of %d') % (k + 1, len(densities)),
                        'channels': [('density', _tr('density'), d, 0.0, 1.0)]}
                if k + 1 < len(densities):
                    body = self._cut_field(d, threshold)
                    body._bounds = self.bounds
                    step['shape'] = body
                    if drawn and k < len(sources) and sources[k][0]:
                        step['surface'] = (sources[k][0], sources[k][1], float(threshold))
                steps.append(step)
            shown = {'_color_fields': [('density', _tr('density'), self.density, 0.0, 1.0)],
                     '_color_field_name': 'density', '_shape': self.shape(threshold)}
            if steps:
                shown['_color_steps'] = steps
                shown['_color_step'] = len(steps) - 1
            self._shown = shown
        out = colored(shown['_shape'], self.density, range=(0.0, 1.0), label=_tr('density'))
        out.__dict__.update({k: v for k, v in shown.items() if k != '_shape'})
        out._color_detail = float(getattr(self, 'element_size', 0) or 0)
        return out

    def _set_problem(self, analyses, outputs, program, bases, outs, goal):
        ''' What the optimisation was asked, and what the kernel calculated of it at every iteration: bases[measure][iteration] and outs[output][iteration]
            (the objective is minimised: negated when it was maximised) '''
        self._analyses = analyses
        self._outputs = outputs
        self._program = program
        self._bases = bases
        self._outs = outs
        self.goal = goal
        self.analysis = None
        self.values = OrderedDict()
        self.feasible = None

    def _verify_into(self):
        ''' The analyses of the optimized part (its numbers are the true ones: `best.max_von_mises`), and the numbers that were asked for, as the part has them '''
        from fieldes.stdlib import optimization_measures as measures
        # A part whose members are thinner than the analysis' mesh cannot be held by its supports or reached by its loads there: the cut is
        # lowered a little at a time (thicker members, a little more material) until the analysis can run, and the result says so
        first = self.keep_threshold()
        cut_before = getattr(self, '_cut', None)
        results = None
        while results is None:
            try:
                results = self.verify()
            except FeaError as e:
                message = str(e).strip().split('\n')[0]
                t = self.keep_threshold()
                if ("don't hold" not in message and "doesn't touch" not in message) or t <= 0.06 or t < 0.6 * first:
                    self._keep_threshold = first
                    self._cut = cut_before
                    self._verify_error = message
                    return
                self._keep_threshold = max(0.05, t - max(0.02, 0.1 * t))
        if self.keep_threshold() != first:
            self._cut = ((cut_before or (first, first))[0], self.keep_threshold())
        results = results if isinstance(results, list) else [results]
        self._verified = results
        self.analysis = results[0] if len(results) == 1 else results
        base_volume = float(self._analyses[0].result.volume)
        self._part_share = float(results[0].volume) / base_volume if base_volume > 0 else None      # (what the cut part keeps of the part, as its analysis has it)

        def leaf(key):
            if key[0] == 'fraction':
                return float(results[0].volume) / base_volume if base_volume > 0 else 0.0
            return measures.true_value(key, results)

        sign = -1.0 if self.goal == 'maximize' else 1.0
        self.feasible = True
        for o in self._outputs:
            try:
                if o.role == 'objective':
                    self.values[o.text] = sign * measures.evaluate(o.node, leaf)
                else:
                    self.values[o.text] = measures.evaluate(o.value, leaf)
                    if not measures.evaluate(o.node, leaf) <= 1e-3:
                        self.feasible = False
            except Exception as e:
                self.values[o.text] = None
                self.feasible = None
                self._verify_error = str(e).strip().split('\n')[0]
        self.objective = next(iter(self.values.values()), None)

    def __getattr__(self, name):
        # (what the analysis of the optimized part has: its numbers and fields -- `best.max_von_mises`, `best.von_mises`)
        if name.startswith('_') or self.__dict__.get('_forwarding'):
            raise AttributeError(name)
        analysis = self.__dict__.get('analysis')
        if analysis is None or isinstance(analysis, list):
            raise AttributeError(name)
        self.__dict__['_forwarding'] = True
        try:
            return getattr(analysis, name)
        finally:
            self.__dict__['_forwarding'] = False

    def table(self):
        ''' What was asked, at the first iteration, at the last, and as the optimized part has it, as text '''
        sign = -1.0 if self.goal == 'maximize' else 1.0
        rows = [['', _tr('first iteration'), _tr('last iteration'), _tr('optimized part')]]

        def text(v):
            return '-' if v is None else ('%.4g' % v)
        for k, o in enumerate(self._outputs):
            series = self._outs[k] if k < len(self._outs) else []
            if o.role == 'objective' and self.goal == 'satisfy':
                continue
            if o.role == 'objective':
                a, b = (sign * series[0], sign * series[-1]) if series else (None, None)
                rows.append([('%s %s' % (self.goal, o.text)), text(a), text(b), text(self.values.get(o.text))])
            else:
                rows.append([o.text, text(series[0]) if series else '-', text(series[-1]) if series else '-', text(self.values.get(o.text))])
        widths = [max(len(r[i]) for r in rows) for i in range(4)]
        return '\n'.join('  '.join(c.ljust(w) if i == 0 else c.rjust(w) for i, (c, w) in enumerate(zip(r, widths))).rstrip() for r in rows)

    def __repr__(self):
        h = self.history
        if not getattr(self, '_outputs', None):
            return _tr('Topology optimization: %.0f %% of the part kept (%d iterations, %.1f s)') % (100 * self.volume_fraction, self.iterations, self.seconds)
        if self.goal == 'satisfy':
            lines = [_tr('Topology optimization: %.0f %% of the part kept (%d iterations, %.1f s)') % (100 * self.volume_fraction, self.iterations, self.seconds)]
        else:
            lines = [_tr('Topology optimization: %s %s %.4g -> %.4g, %.0f %% of the part kept (%d iterations, %.1f s)') % (
                self.goal, self._outputs[0].text, h[0] if h else 0, h[-1] if h else 0, 100 * self.volume_fraction, self.iterations, self.seconds)]
        if len(self._outputs) > 1 or self.analysis is not None:
            lines.append(self.table())
        cut = getattr(self, '_cut', None)
        if cut:
            note = _tr('  The part is cut from the design at the density %.2f, not %.2f (the cut that keeps exactly the volume): at the lower density it is one piece that can be analysed.') % (cut[1], cut[0])
            share = getattr(self, '_part_share', None)
            if share is not None:
                note += ' ' + _tr("It has %.0f %% of the part's volume, the design %.0f %%.") % (100 * share, 100 * self.volume_fraction)
            lines.append(note)
        if self.feasible is False:
            lines.append(_tr('  A constraint is broken by the optimized part (the design is the optimiser\'s own; the part is cut from it).'))
        if self.analysis is None and getattr(self, '_verify_error', None):
            lines.append(_tr('  The optimized part could not be analysed again: %s (its members may be thinner than this mesh can follow: try a smaller element_size).') % self._verify_error)
        return '\n'.join(lines)


_topo_cache = OrderedDict()


def _is_cases(loads):
    ''' loads given as load cases: a list of lists of loads '''
    return (isinstance(loads, (list, tuple)) and len(loads) > 0 and
            all(isinstance(c, (list, tuple)) for c in loads))


def _condition_places(supports, loads):
    ''' The places where the supports and loads act, as one field (negative inside them); None when no support or load has a place '''
    regions = [s.region for s in supports] + [l.region for case in (loads if _is_cases(loads) else [loads]) for l in case
                                              if isinstance(l, _Force)]
    inside = None
    for r in regions:
        r = _united(r) if isinstance(r, (list, tuple)) else r
        inside = r if inside is None else inside.min(r)
    return inside


def _face_columns(places, part, h):
    ''' The wall behind the flat faces of the part that the supports and loads act on: (the columns, the space behind the planes) -- two fields, each
        negative inside.  What holds the part (or pushes on it) lies on the far side of a face's plane, and nothing of the design can be there.  The
        COLUMNS are the outline of each face carried out from it, away from the part, along its normal: the hole the mesh has, so that the face is a
        surface of the mesh, the one surface a support holds and a load acts on.  The SPACE BEHIND is the half-space beyond the face's plane within
        five elements of its outline (where the density, blurred by the filter, would otherwise curl round the plate's edge, in front of the plane, to
        the nodes of the edge): its elements are held empty.  Beside the plate on the part's side of the plane nothing is forbidden.  (A box, a
        column alone and a half-space with the slab behind the face held were tried: the design wrapped round the plate, or stood off from it.)

        A face is found by looking.  The part is sampled inside the places (a little wider than they are, as the conditions take the nodes within
        half an element of them); in each column of samples along an axis, the one farthest out is the face of the part when a little farther out
        there is none.  It is the plate's face when it is flat and its outline is a good part of the largest one (the strips of its sides that the
        box happens to take in are not).  None when the places have no such face -- a bolt hole that carries a load: then the zone is the places
        themselves, as it was (see _hole_of_places) '''
    from fieldes.stdlib.fields import evaluate, _dist
    reach = 5.0 * h                         # (how far beyond the face's outline the space behind its plane is kept empty: a few filter radii)
    try:
        lo, hi = _bounds(places)
    except Exception:
        return None
    lo = [float(lo[i]) - h for i in range(3)]
    hi = [float(hi[i]) + h for i in range(3)]
    step = h / 3.0
    while True:
        n = [max(3, int(math.ceil((hi[i] - lo[i]) / step))) for i in range(3)]
        if n[0] * n[1] * n[2] <= 60000:
            break
        step *= 1.2
    d = [(hi[i] - lo[i]) / n[i] for i in range(3)]
    pts = [(lo[0] + (i + 0.5) * d[0], lo[1] + (j + 0.5) * d[1], lo[2] + (k + 0.5) * d[2])
           for k in range(n[2]) for j in range(n[1]) for i in range(n[0])]
    wide = places - 0.5 * h
    inside = [a < 0.0 and b < 0.0 for a, b in zip(evaluate(part, pts), evaluate(wide, pts))]

    # for each axis and each way out along it: the samples of the part that lie farthest out in their column, and a point a little farther out
    found = []
    probes = []
    for a in range(3):
        u, v = [b for b in range(3) if b != a]
        for way in (-1, 1):
            order = range(n[a]) if way < 0 else range(n[a] - 1, -1, -1)
            cols = []
            for iu in range(n[u]):
                for iv in range(n[v]):
                    for ia in order:
                        index = [0, 0, 0]
                        index[a], index[u], index[v] = ia, iu, iv
                        if inside[index[0] + n[0] * (index[1] + n[1] * index[2])]:
                            cols.append((iu, iv, ia))
                            break
            found.append((a, way, u, v, cols))
            for iu, iv, ia in cols:
                p = [0.0, 0.0, 0.0]
                p[a] = lo[a] + (ia + 0.5) * d[a] + way * 0.75 * h
                p[u] = lo[u] + (iu + 0.5) * d[u]
                p[v] = lo[v] + (iv + 0.5) * d[v]
                probes.append(tuple(p))
    outside = evaluate(part, probes) if probes else []
    candidates = []
    m = 0
    for a, way, u, v, cols in found:
        coords = []
        rep = None
        for iu, iv, ia in cols:
            if outside[m] > 0.0:
                coords.append(lo[a] + (ia + 0.5) * d[a])
                if rep is None:
                    rep = (lo[u] + (iu + 0.5) * d[u], lo[v] + (iv + 0.5) * d[v])
            m += 1
        if coords:
            candidates.append((len(coords) * d[u] * d[v], max(coords) - min(coords) <= 2.5 * d[a], a, way, coords, (u, v, rep)))
    if not candidates:
        return None
    biggest = max(c[0] for c in candidates)
    zone = behind = None
    for area, flat, a, way, coords, where in candidates:
        if area < 0.3 * biggest:
            continue
        if not flat:
            return None
        # (the plane the column is cut at is the sample farthest in: it lies inside the part, which covers whatever of the column is there)
        inner = min(coords) if way > 0 else max(coords)
        # The plane of the face itself, found to a thousandth of an element by halving, along the axis through one column of the face
        u, v, rep = where
        probe = [0.0, 0.0, 0.0]
        probe[u], probe[v] = rep
        inside_at, outside_at = inner, inner + way * 0.75 * h
        for _ in range(14):
            probe[a] = 0.5 * (inside_at + outside_at)
            if evaluate(part, [tuple(probe)])[0] < 0.0:
                inside_at = probe[a]
            else:
                outside_at = probe[a]
        face = 0.5 * (inside_at + outside_at)
        axes = [Shape.X(), Shape.Y(), Shape.Z()]
        coord = axes[a]
        axes[a] = Shape.wrap(inner)
        outline = part.remap(*axes).max(wide.remap(*axes))
        column = outline.max((coord - inner) * float(-way))
        # (how far a point of the face's plane lies from the face's outline: its distance to the part there, and to the places)
        near = (_dist(part).remap(*axes) - reach).max(wide.remap(*axes) - reach)
        beyond = near.max((coord - face) * float(-way))
        zone = column if zone is None else zone.min(column)
        behind = beyond if behind is None else behind.min(beyond)
    return zone, behind


def _hole_of_places(supports, loads, part, h):
    ''' What a design that may grow leaves empty at the places where the supports and loads act, as a field (negative inside): nothing is added
        there, so that a load acts on the part's own surface and the load case is the one that was given.  The columns over the flat faces they
        act on (_face_columns); where the places have no such face (a bolt hole that carries a load stays open) the places themselves.  None when
        no support or load has a place '''
    inside = _condition_places(supports, loads)
    if inside is None:
        return None
    found = _face_columns(inside, part, h)
    return (inside, None) if found is None else found


def _skin_space(part, distance, clear):
    ''' The part and the part offset by `distance` mm -- except where the field `clear` is positive (see _hole_of_places: `clear` is its negative) '''
    from fieldes.stdlib.fields import _dist
    skin = _dist(part) - distance
    if clear is not None:
        skin = skin.max(clear)
    return part.min(skin)


def _symmetry_request(symmetry, element, what='topology_optimization'):
    ''' The symmetry argument of an optimisation (`what`) as (look for planes by itself, [(axis, position or None)], text for the key) '''
    if symmetry is None or symmetry is False:
        return False, [], 'none'
    if symmetry is True or (isinstance(symmetry, str) and symmetry.lower() == 'auto'):
        return element == 'tet', [], 'auto' if element == 'tet' else 'none'
    if isinstance(symmetry, dict):
        items = list(symmetry.items())
    elif isinstance(symmetry, str):
        items = [(c, None) for c in symmetry.lower()]
    elif isinstance(symmetry, (list, tuple)):
        items = [(c, None) for c in symmetry]
    else:
        raise TypeError("%s: symmetry is None, 'auto', 'x', 'y', 'z' (or several of them: 'xz'), or {'z': 0.0}" % what)
    planes = []
    for name, at in items:
        name = str(name).lower()
        if name not in ('x', 'y', 'z'):
            raise ValueError("%s: symmetry is None, 'auto', 'x', 'y', 'z' (or several of them: 'xz'), or {'z': 0.0}" % what)
        planes.append(('xyz'.index(name), None if at is None else float(at)))
    if element != 'tet':
        raise FeaError("%s: symmetry needs the tetrahedral optimization (element='tet')" % what)
    planes.sort(key=lambda p: p[0])
    return False, planes, ','.join('%s%s' % ('xyz'[a], '' if at is None else '@%.6g' % at) for a, at in planes)


def _optimization_analyses(analysis, what):
    ''' The analyses an optimisation is made for: each the result of an analysis made in this script (static_analysis, modal_analysis ...), with the
        arguments it was made with -- the part, the supports, the loads, the material --, which are the problem the optimisation designs for '''
    import inspect
    from fieldes.stdlib import optimization_measures as measures
    items = list(analysis) if isinstance(analysis, (list, tuple)) else [analysis]
    if not items:
        raise TypeError('%s is given the result of an analysis (or a list of them): static_analysis, modal_analysis ...' % what)
    out = []
    for k, r in enumerate(items):
        recipe = getattr(r, '_recipe', None) if hasattr(r, '__dict__') else None
        if recipe is None:
            raise TypeError('%s is given the result of an analysis made in this script (static_analysis, modal_analysis ...) -- %s is not one' % (what, type(r).__name__))
        fn, args, kwargs = recipe
        bound = inspect.signature(fn).bind(*args, **kwargs)
        bound.apply_defaults()
        if isinstance(r, ModalResult):
            kind = 'modal'
        elif isinstance(r, Result):
            kind = 'static'
        elif type(r).__name__ == 'FluidResult':
            kind = 'flow'
        elif type(r).__name__ == 'ThermalResult':
            kind = 'thermal'
        else:
            raise FeaError('%s: an optimisation for the result of %s is not made here' % (what, type(r).__name__))
        a = measures.Analysis(k, r, kind)
        a.call, a.arguments = fn, dict(bound.arguments)
        if kind == 'thermal':
            a.supports, a.loads = [], []            # (a heat conduction has its own conditions: the thermal analysis' regions)
        elif kind == 'flow':
            # (a body in a flow has no supports or loads; its volume, which 'volume' is a share of, is counted when it is asked for)
            a.supports, a.loads = [], []
            a.body_volume = lambda body=a.arguments.get('body'): _body_volume(body)
        else:
            a.supports = _given(a.arguments.get('supports', []), 'supports', what)
            a.loads = _given(a.arguments.get('loads', []), 'loads', what) if kind == 'static' else []
        out.append(a)
    return out


def _body_volume(body):
    from fieldes.stdlib.fields import volume_of
    return float(volume_of(body))


def _same_value(a, b):
    if a is b:
        return True
    if isinstance(a, Shape) or isinstance(b, Shape):
        return isinstance(a, Shape) and isinstance(b, Shape) and a.ptr == b.ptr
    return a == b


def topology_optimization(analysis, constraints=(), iterations=None, filter_radius=None, keep=None,
                          avoid=None, grow=False, grow_limit=None, extrude=None, penalty=3.0, sharpness=16.0, symmetry='auto', tolerance=1e-5, cache=True):
    ''' Topology optimization: the layout of material, within the part of `analysis`, that is best for what is asked of it.

            stress = static_analysis(part, [fixed(base)], [force(lug, (0, 0, -300))], steel)
            best = topology_optimization(stress, constraints=[minimize('compliance'), 'volume_fraction <= 0.3'])
            lighter = topology_optimization(stress, constraints=[minimize('mass'), 'max_von_mises <= 120', 'max_displacement <= 0.4'])

        analysis      the result of an analysis of this script -- static_analysis, modal_analysis, thermal_analysis -- or a list of them: the part is the design
                      space, and its supports, loads and material are the problem it is designed for.  A list is several load cases (the
                      same part under other loads and supports), or a stress case and a vibration case: [stress, vibration].  The result of a
                      fluid_analysis is a BODY IN A FLOW: the body is the start of the design, the fluid domain its space, and the numbers
                      are drag_force and lift_force (the force of the flow on the body along it and across it, N) and volume_fraction (of
                      the body's own volume) -- `topology_optimization(wing, constraints=[maximize('lift_force / drag_force'),
                      'volume_fraction <= 0.4'])`.  The result of a thermal_analysis (tetrahedral elements) is a HEAT CONDUCTION:
                      the part is the design space, the held temperatures, heat inputs and convecting surfaces are the problem, and the numbers
                      are max_temperature, min_temperature, max_heat_flux, input_temperature (the temperature the heat comes in at: the
                      heat-weighted mean of the heat inputs), temperature(x, y, z), volume, mass and volume_fraction --
                      `topology_optimization(sink, constraints=[minimize('max_temperature'), 'volume_fraction <= 0.3'])`
        constraints   what the design is for, a list.  Comparisons it keeps: 'volume_fraction <= 0.3', 'max_von_mises <= 90', 'frequencies[0] >= 80',
                      an exact value 'frequencies[0] == 100' (kept to within a thousandth), a range '80 <= frequencies[0] <= 120' -- or a
                      function of the result that is 0 or less when kept.  And at most one goal: minimize(x) makes x as small as it can be,
                      maximize(x) as large -- x a number of the analyses' results ('compliance', 'mass', 'max_von_mises', 'frequencies[0]'
                      ...), a formula of them ('mass + 0.01 * compliance', 'lift_force / drag_force') or a function of the result
                      (`lambda r: r.max_von_mises / r.mass`).  Without a goal the design only has to keep the comparisons.  The numbers of
                      the design that the formulas can read: volume_fraction (the share of the part's volume it uses), volume, mass, and
                      from a static analysis compliance, max_von_mises, max_displacement, safety_factor and ux(x, y, z), uy, uz, displacement
                      (the displacement at a place), from a modal analysis frequencies[i].  With several analyses a number that more than one
                      offers is, in a goal, the SUM of theirs, and a comparison on it is kept in each of them
        keep:         regions (Shapes, or a list) that must stay solid -- e.g. bolt bosses, mounting faces; the material around supports
                      and loads always stays
        avoid:        regions that must stay empty
        grow:         False (default: the design stays inside the part) or True: the design is NOT limited to the part -- it starts as the part
                      and puts material anywhere, and takes it away anywhere, where that makes it best.  volume_fraction is then of the
                      part's own volume (1 spends the same material, 1.2 spends 20 % more).  The design space follows the design: the
                      moment material reaches the edge of the space the run stops, the space is made again round the material, and the
                      same run goes on in it -- the same iteration, sharpening and step (so the space costs what the material costs, not
                      what a shell of some thickness would, and the design is never held against a wall while it runs); nothing is added
                      behind the face of the part that a support or a load acts on -- over the face, and past the plane of the face a few
                      elements round its outline: what holds the part, or pushes on it, is there -- and nothing else is forbidden: the supports
                      hold that face only, the load case is the one that was given, and on the part's side of the plane the design is as the
                      stresses ask, also round the plate.  A place that has no flat face (a bolt hole that carries a load) is kept clear as
                      the place itself.
                      Only the iterations stop the growth: if they run out with the design
                      still at the edge of its space, the output says so and the part is cut there
        grow_limit:   mm, with grow=True: the most the design may reach out from the part.  A restriction on top of grow, never the base
                      of it: left out, nothing limits how far the design grows
        extrude:      'x', 'y' or 'z' -- the design is the same all along that axis (outside the keep / avoid regions): a profile to
                      extrude, or to cut right through from one side
        iterations:   at most this many design updates (default 100; a body in a flow 80: it is sharpened a little at every iteration): it stops sooner when the design has settled (from the 15th on,
                      once the sharpness has been reached, when the design hardly changes or the objective stays flat for 5 iterations
                      -- and every constraint is kept).  It also sets the pace of the sharpening and the size of the steps
        filter_radius: the smallest member size scale, mm (default 1.5 elements)
        penalty:      the power the density is raised to in the stiffness (default 3)
        sharpness:    how crisp the design is (default 16): the density is pushed towards 0 and 1 more and more as the iterations go
                      on, up to this much.  1 leaves the density as the filter makes it (soft edges)
        symmetry:     'auto' (default), None, 'x' / 'y' / 'z' or several ('xz'), or {'z': 0.0} with the plane's position.  A design is
                      kept symmetric about a plane when the part is, and so are its supports, its loads and its keep / avoid regions

        The numbers are the kernel's own: it calculates every measure with its derivative to the density of every element (the
        compliance and the frequencies from the solution, the displacement and the stress with one more solve each), differentiates the
        formulas, and moves the design with the method of moving asymptotes on the objective and every constraint.  The stress is the
        element's von Mises stress, relaxed at low density, and its peak is the p-norm of them brought to the peak.

        Returns a TopologyResult: .density (a field), .shape() (the optimized part), .analysis (the analysis of that part: its numbers
        -- `best.max_von_mises` -- are the true ones; a list with several), .values (what was asked, as the optimized part has it),
        .feasible, .history (the objective per iteration), .verify().  An unchanged problem is cached, so re-running a script is instant. '''
    # (a body in a flow is designed on the same optimiser, with the flow's own measures: fluid.py)
    from fieldes.stdlib import fluid as _fluid
    probe = analysis[0] if isinstance(analysis, (list, tuple)) and len(analysis) else analysis
    if isinstance(probe, _fluid.FluidResult):
        return _fluid.topology_optimization_flow(analysis, constraints, iterations=iterations, filter_radius=filter_radius, keep=keep, avoid=avoid,
                                                 grow=grow, grow_limit=grow_limit, extrude=extrude, penalty=penalty, sharpness=sharpness,
                                                 symmetry=symmetry, tolerance=tolerance, cache=cache)
    if type(probe).__name__ == 'ThermalResult':
        from fieldes.stdlib import thermal as _thermal
        return _thermal.topology_optimization_thermal(analysis, constraints, iterations=iterations, filter_radius=filter_radius, keep=keep, avoid=avoid,
                                                      grow=grow, grow_limit=grow_limit, extrude=extrude, penalty=penalty, sharpness=sharpness,
                                                      symmetry=symmetry, tolerance=tolerance, cache=cache)
    iterations = 100 if iterations is None else iterations
    from fieldes.stdlib import optimization_measures as measures
    max_iterations = 20000      # (the equation solver's own limit, never reached by a problem that has a solution: not an argument -- it stops by itself when the residual is below `tolerance`)
    if getattr(lib, 'libfive_tetfea_optimize', None) is None or getattr(lib, 'libfive_tetfea_opt_measure', None) is None:
        raise FeaError('this FielDes library is too old for topology optimization')
    if not constraints:
        raise TypeError("topology_optimization: say what the design is for -- constraints=[minimize('mass'), 'max_von_mises <= 90', ...]")
    analyses = _optimization_analyses(analysis, 'topology_optimization')
    given = [a.result for a in analyses]
    first = analyses[0]
    # One design space, one material, one mesh for every analysis: the cases differ in their supports and loads
    part = first.arguments['shape']
    if not isinstance(part, Shape):
        raise TypeError('topology_optimization: the part must be a Shape')
    material = first.arguments['material']
    for a in analyses[1:]:
        if not _same_value(a.arguments['shape'], part):
            raise FeaError('topology_optimization: the analyses are of different parts -- they are cases of one design: the same part, other supports and loads')
        m = a.arguments['material']
        if not (_same_value(m.E, material.E) and _same_value(m.nu, material.nu) and _same_value(m.density, material.density)):
            raise FeaError('topology_optimization: the analyses have different materials -- a design is of one material')
    element_size = next((a.arguments.get('element_size') for a in analyses if a.arguments.get('element_size') is not None), None)
    bounds = next((a.arguments.get('bounds') for a in analyses if a.arguments.get('bounds') is not None), None)
    all_supports = [s for a in analyses for s in a.supports]
    static = [a for a in analyses if a.kind == 'static']
    for a in static:
        if not a.loads:
            raise FeaError('topology_optimization: a static analysis has loads')
        for l in a.loads:
            if isinstance(l, _Thermal):
                raise FeaError("thermal_expansion isn't supported in topology optimization (its load changes with the design)")
            if not isinstance(l, (_Force, _Gravity)):
                raise TypeError('loads must be force(...) or gravity(...) items')
    gravity = [any(isinstance(l, _Gravity) for l in a.loads) for a in static]
    if any(gravity) and not all(gravity):
        raise FeaError('topology_optimization: gravity acts in every static analysis of a design: give it to all of them or to none')
    if any(gravity) and not _density_given(material):
        raise FeaError('gravity needs a material with a density')
    all_loads = [a.loads for a in static]
    _use_conditions(all_supports + [l for ls in all_loads for l in ls], part)
    mirrors = _symmetry_request(symmetry, 'tet')
    keep = [] if keep is None else list(keep if isinstance(keep, (list, tuple)) else [keep])
    avoid = [] if avoid is None else list(avoid if isinstance(avoid, (list, tuple)) else [avoid])
    # (what the part has excluded stays as it is: its locked fields are regions to keep)
    keep = keep + keep_regions(part)
    axes = {None: -1, 'x': 0, 'y': 1, 'z': 2}
    if extrude not in axes:
        raise ValueError("extrude is None, 'x', 'y' or 'z'")
    if grow is not None and not isinstance(grow, bool):
        raise ValueError('topology_optimization: grow is True or False now (the design may grow anywhere, or stays in the part); '
                         'the most it may reach out is a restriction of its own: grow=True, grow_limit=5')
    free = bool(grow)
    limit = None if grow_limit is None else float(grow_limit)
    if limit is not None:
        if not free:
            raise ValueError('topology_optimization: grow_limit restricts how far a design that may grow reaches out: it needs grow=True')
        if not limit > 0:
            raise ValueError('topology_optimization: grow_limit is a distance of more than 0 mm')

    # What is asked of the design: the objective and the constraints as trees of measures, and the program the kernel is given
    if isinstance(constraints, str) or callable(constraints):
        constraints = [constraints]

    def baseline_leaf(key):
        return 1.0 if key[0] == 'fraction' else measures.true_value(key, given)

    def baseline(node):
        return measures.evaluate(node, baseline_leaf)

    outputs, goal = measures.outputs_of(constraints, analyses, baseline)
    program = measures.Program(outputs)
    if ('fraction',) not in program.measures:
        program.measures.append(('fraction',))                      # (the share of the part the design uses is always kept: the result reports it)
    start = program.start_fraction()

    # The whole problem by its content, before anything is built (see static_analysis)
    ekey = problem_key('topology', part=part, part_bounds=getattr(part, '_bounds', None),
                       analyses=[(a.kind, a.supports, a.loads, a.arguments.get('modes', 0)) for a in analyses], symmetry=mirrors[2],
                       material=material, element_size=element_size, bounds=bounds, program=program.key(), start=start,
                       iterations=int(iterations), filter_radius=filter_radius, keep=keep, avoid=avoid, grow=free, grow_limit=limit,
                       extrude=extrude, penalty=float(penalty), sharpness=float(sharpness), max_iterations=max_iterations,
                       tolerance=tolerance, algorithm=16) if cache else None        # (what the optimiser gives for the same problem: bumped when it changes -- 3: the steps that make the design worse are taken back; 4: the stress relaxation, the sharpening that waits; 5: material is held when it joins a support; 6: the stress exponent from the penalty, the cut that keeps one piece; 7: the cut volume counted exactly, free growth; 8: free growth without a zone round the supports and loads and without a cap of design spaces; 9: the design space follows the design inside one run; 10: the supports and loads act on the part only, not on the walls of the holes the grown space has round them; 11: every design space has the mesh lattice of the first; 12: the held elements do not count in the density filter; 13: the hole at a support or a load is the column over its face, not the box it was drawn as; 14: (a half-space behind the face's plane: tried, taken out); 15: a support holds the face only -- the nodes on the surface of the mesh -- not the slab of the part behind it; 16: the space behind the plane of the face, beside the plate, is held empty and the part is cut within it)
    if ekey is not None and ekey in _topo_cache:
        _topo_cache.move_to_end(ekey)
        cached = _topo_cache[ekey]
        if cached.part is part:
            return cached
        r = object.__new__(TopologyResult)
        r.__dict__.update(cached.__dict__)
        r.part = part
        return r

    lo, hi = bounds if bounds is not None else _bounds(part)
    size = [hi[i] - lo[i] for i in range(3)]
    if element_size is None:
        element_size = max(size) / 40.0
    element_size = float(element_size)
    fn = lambda name: getattr(lib, 'libfive_tetfea_' + name)
    kind = 'tetfea'
    settings = {'volume_fraction': start, 'penalty': float(penalty), 'filter_radius': float(filter_radius or 0.0), 'iterations': int(iterations),
                'extrude': axes[extrude], 'element': 'tet', 'sharpness': float(sharpness), 'symmetry': mirrors[2],
                'grow': int(free), 'grow_limit': 0.0 if limit is None else limit}

    # The design space.  A design that may not grow fills the part.  One that may grow starts in the part and a thin skin round it, and the space is made
    # again, round the material, when the material reaches its edge (below): so the design is limited by nothing but what it is asked to keep, and what it
    # costs is what its material costs, not what a shell of some thickness round the whole part would
    skin = 3.0 * element_size                   # (the skin round the part that the design may put material into at once)
    reach = 4.0 * element_size                  # (how far past the material the next design space goes)
    hole = _hole_of_places(all_supports, all_loads, part, element_size) if free else None
    zone, behind = hole if hole is not None else (None, None)
    clear = None if zone is None else -zone                     # (the hole the mesh has: the wall itself, over the face)
    # What the part is cut within: that, and the space behind the plane of the face, beside the plate -- what holds the part (or pushes on it) is there.
    # The mesh keeps that space (no hole: the face stays the one surface the support holds); its elements are held empty
    clear_cut = clear if behind is None else -(zone.min(behind))
    if behind is not None:
        avoid = avoid + [behind]
    held = None                                 # (where the edge of a design space is held in, and is not made larger against: see below)
    if free:
        from fieldes.stdlib.fields import _dist
        origin = _dist(part)                    # (what lies outside the part, and how far: the field of an imported part is only a bound of the distance)
        # The edge of a design space is moved outwards, as the design reaches it, nearly everywhere.  It is held in only by the hole the supports and
        # loads have in it (the columns over the faces they act on: nothing is added there) and by the limit, when one is given: material that
        # stands against those does not call for a larger space, since there is none to make there
        held = None if zone is None else zone - 2.0 * element_size
        if limit is not None:
            far = -(origin - (limit - 2.0 * element_size))
            held = far if held is None else held.min(far)

    def space_of(design=None):
        ''' (the design space, the box it is meshed in).  `design`: the surface of the design found in the last design space, as an exact distance field
            (negative inside) with its box -- the next space is the part, its skin, and everything within `reach` of the design; never where a support or
            a load acts, and never past `grow_limit` '''
        if not free:
            return part, list(lo), list(hi)
        d = skin if limit is None else min(skin, limit)
        space = _skin_space(part, d, clear)
        lo_, hi_ = [lo[i] - d for i in range(3)], [hi[i] + d for i in range(3)]
        if design is not None:
            surface, slo, shi = design
            near = surface - reach
            if clear is not None:
                near = near.max(clear)
            if limit is not None:
                from fieldes.stdlib.fields import _dist
                near = near.max(_dist(part) - limit)
            space = space.min(near)
            lo_ = [min(lo_[i], slo[i] - reach) for i in range(3)]
            hi_ = [max(hi_[i], shi[i] + reach) for i in range(3)]
        if design is not None:
            # The mesh is a lattice laid over the box from its lower corner, and what it resolves of a thin gap -- the hole a place has beside the part is
            # one or two elements wide -- depends on where the lattice planes fall.  Every design space of a run has the lattice of the first: a gap that
            # the first resolved is resolved in the others (a space with its corner a little elsewhere bridged it with tetrahedra, held to the part)
            base = [lo[i] - d for i in range(3)]
            lo_ = [base[i] - math.ceil(max(0.0, base[i] - lo_[i]) / element_size - 1e-9) * element_size for i in range(3)]
        if limit is not None:
            lo_ = [max(lo_[i], lo[i] - limit) for i in range(3)]
            hi_ = [min(hi_[i], hi[i] + limit) for i in range(3)]
        space._bounds = (tuple(lo_), tuple(hi_))
        return space, lo_, hi_

    def region_of(space_lo, space_hi, fenced=False):
        ''' What a design that may grow may fill, as a field (negative inside): the part, and everything that is not inside a place where a support or a
            load acts on (see _hole_of_places), and within `grow_limit` of the part when one is given.  The part of the design is cut from its density within
            this, as the part of a design that may not grow is cut within the part: its faces are those of the part, of the places of the conditions and
            of the limit, fields with a normal of their own -- not the edge of the mesh, which the density has no face at.  Nothing else bounds it: no
            wall.  Only when the iterations ran out while the design still reached the edge of its design space (`fenced`: it is said) the part is
            cut at the box of that space -- what the design had room for -- because a density goes on beyond its mesh as it is at its edge '''
        region = part.min(clear_cut) if clear_cut is not None else None
        if limit is not None:
            from fieldes.stdlib.fields import _dist
            bound = _dist(part) - limit
            region = bound if region is None else region.max(bound)
        if fenced:
            from fieldes.stdlib.shapes import box
            fence = box(tuple(space_lo), tuple(space_hi))
            region = fence if region is None else region.max(fence)
        return region

    def design_surface(ptr, level=0.2):
        ''' The surface of the design the kernel found -- where its density is `level` -- as an exact distance field (negative inside) and its box; None
            when there is nothing of it '''
        counts = (ctypes.c_int64 * 2)()
        if not fn('surface')(ptr, float(level), counts) or counts[0] < 4 or counts[1] < 4:
            return None
        verts = (ctypes.c_float * (3 * counts[0]))()
        tris = (ctypes.c_uint32 * (3 * counts[1]))()
        fn('surface_copy')(ptr, verts, tris)
        from fieldes.ffi import libfive_mesh_import_info_t
        info = libfive_mesh_import_info_t()
        tree = lib.libfive_mesh_from_arrays(verts, counts[0], tris, counts[1], 1.0, ctypes.byref(info))
        if not tree:
            return None
        b = info.bounds
        return Shape(tree), (b.X.lower, b.Y.lower, b.Z.lower), (b.X.upper, b.Y.upper, b.Z.upper)

    def stage_data(p):
        ''' What the kernel calculated at each iteration of one problem: its objective, every measure and every output, and the density after each '''
        hist = (ctypes.c_double * 1000)()
        m = fn('history')(p, hist, 1000)
        history = [hist[i] for i in range(min(m, 1000))]
        bases = [[0.0] * len(history) for _ in program.measures]
        outs = [[0.0] * len(history) for _ in outputs]
        buf = (ctypes.c_double * 1000)()
        for j in range(len(program.measures)):
            n = lib.libfive_tetfea_measure_history(p, 0, j, buf, 1000)
            bases[j] = [buf[i] for i in range(n)]
        for j in range(len(outputs)):
            n = lib.libfive_tetfea_measure_history(p, 1, j, buf, 1000)
            outs[j] = [buf[i] for i in range(n)]
        densities = []
        at = getattr(lib, 'libfive_tetfea_density_at', None)
        for k in range(len(history)):
            f = at(p, k) if at else None
            if not f:
                break
            densities.append(field_shape(f))
        return history, bases, outs, densities

    def finish(handle, ptr, seconds, symmetric=None, saved=None, earlier=(), space=None, fenced=False):
        ''' The result from a solved problem (just optimised, or read back from its file: `symmetric`, what the file says).  `earlier`: the problems
            of the design spaces before this one, when the design grew (each a (pointer, handle)); `space`: (the design space, its box) of this one;
            `fenced`: the iterations ran out with the design still at the edge of its space (see region_of) '''
        if saved is not None:
            fenced = bool(saved.get('fenced'))
        density = field_shape(fn('density')(ptr))
        history, bases, outs, densities = [], [[] for _ in program.measures], [[] for _ in outputs], []
        sources = []
        for p, _h in list(earlier) + [(ptr, handle)]:
            h, b, o, d = stage_data(p)
            history += h
            for j in range(len(bases)):
                bases[j] += b[j]
            for j in range(len(outs)):
                outs[j] += o[j]
            densities += d
            sources += [(ctypes.cast(p, ctypes.c_void_p).value, k) for k in range(len(d))]
        if saved is not None:
            bases, outs = saved['bases'], saved['outputs']
            history = saved.get('history') or history
            share = float(saved['share'])
        else:
            share = bases[program.measures.index(('fraction',))][-1] if history else start
        if space is None:
            box = saved.get('bounds') if saved is not None else None
            space = (None if free else part, box[0] if box else lo, box[1] if box else hi)
        stats = {'volume_fraction': share, 'iterations': len(history), 'seconds': seconds}
        result = TopologyResult(handle, part, density, history, settings, element_size, (list(space[1]), list(space[2])), [], [], material, stats)
        result.domain = region_of(space[1], space[2], fenced) if free else part
        result.fenced = fenced
        result.grow = free
        result.grow_limit = limit
        result.densities = densities
        result._step_sources = sources
        result._stage_handles = [h for _p, h in earlier]        # (their problems are what the steps of the design are drawn from: kept)
        result._set_problem(analyses, outputs, program, bases, outs, goal)
        # The planes the design was kept symmetric about: {'x': [position, how]} -- asked for (1), or found (2: and then said)
        if symmetric is None:
            symmetric = {}
            probe = getattr(lib, 'libfive_tetfea_mirror', None)
            for k, name in enumerate('xyz'):
                at = ctypes.c_double()
                how = probe(ptr, k, ctypes.byref(at)) if probe else 0
                if how:
                    symmetric[name] = [float(at.value), int(how)]
        found = ['%s = %.4g' % (n, v[0]) for n, v in symmetric.items() if v[1] == 2]
        if found:
            print('topology_optimization: the part, its supports and its loads are symmetric about %s: the design is kept symmetric '
                  '(symmetry=None leaves it alone).' % ' and '.join(found))
        result.symmetry = {n: v[0] for n, v in symmetric.items()}
        result._symmetry_info = symmetric
        # A part that falls into pieces cannot do what a part is for: say so, and what to try.  (The scraps are left
        # in the result on purpose: they show what the part would become with more volume.)
        count = getattr(lib, 'libfive_tetfea_pieces', None)
        if count is not None:
            # (a link thinner than a millimetre of the cut surface is not one: see shape(), the density is scaled to mm)
            width = 2.0 * (settings['filter_radius'] or 1.5 * element_size)
            result.pieces = result._join(count, ptr, 1.0 / width)
            if result.pieces > 1:
                print('topology_optimization: the optimised part is in %d separate pieces (nothing joins them). '
                      'Try more material (a looser limit on the volume or the mass) -- the loose scraps show what the part would '
                      'become -- or fewer or smaller keep regions.' % result.pieces)
        # The analyses of the optimized part: its numbers are the true ones (the design's own are the optimiser's)
        result._verify_into()
        return result

    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load(kind, ekey, 'topology optimization')
    if loaded is not None and loaded[1].get('measures'):
        result = finish(_TetHandle(loaded[0]), loaded[0], float(loaded[1].get('seconds', 0)), dict(loaded[1].get('symmetry') or {}),
                        loaded[1]['measures'])
        _topo_cache[ekey] = result
        return result

    def build(space, slo, shi):
        ''' A problem on a design space: the kernel's handle, the conditions given, prepared, and told what the design is for '''
        region = libfive_region_t()
        pad = 1e-6 * max(max(shi[i] - slo[i] for i in range(3)), 1e-9)
        for i, axis in enumerate((region.X, region.Y, region.Z)):
            axis.lower, axis.upper = slo[i] - pad, shi[i] + pad
        ptr = lib.libfive_tetfea_new(space.ptr, region, element_size, _number(material.E, 1.0), material.nu)
        handle = _TetHandle(ptr)
        if free:
            lib.libfive_tetfea_set_origin(ptr, origin.ptr)
            # (the edge of the design space is what lies farther from the part than the mesh's own slack beside its surface)
            lib.libfive_tetfea_set_frontier_distance(ptr, 2.0 * element_size)
            if held is not None:
                lib.libfive_tetfea_set_frontier_blocked(ptr, held.ptr)
            # The supports and loads act on the part -- on its surface and inside it -- and nowhere on what the design space adds: the hole the space has
            # round the place of a condition has walls on the edge of that place, and a support that held them would anchor material to somewhere that is
            # not on the part, a load that took them would carry a share of the force off it (the node or face has to lie within half an element
            # of the part, the mesh's own slack beside its surface and at its corners)
            lib.libfive_tetfea_set_condition_body(ptr, origin.ptr, 0.5 * element_size)
            # ... and a support holds the face where the part meets what holds it (the surface of the mesh), not the slab of the part behind it that its
            # region takes in: material put against that slab, or curled round the plate's edge, would be clamped to held nodes for nothing
            if zone is not None:
                lib.libfive_tetfea_set_condition_surface(ptr, 1)
        lib.libfive_tetfea_set_sharpness(ptr, float(sharpness))
        if mirrors[0]:
            lib.libfive_tetfea_set_symmetry_auto(ptr, 1)
        for axis, at in mirrors[1]:
            lib.libfive_tetfea_add_mirror(ptr, axis, 0.0 if at is None else float(at), 1 if at is None else 0)
        _material_fields(ptr, material)
        several = len(analyses) > 1
        gravity_set = False
        for a in analyses:
            for s in a.supports:
                if not isinstance(s, _Support):
                    raise TypeError('supports must be fixed(...) items')
                if several:
                    lib.libfive_tetfea_add_support_case(ptr, s.region.ptr, *_support_args(s), a.index)
                else:
                    lib.libfive_tetfea_add_support(ptr, s.region.ptr, *_support_args(s))
            if a.kind == 'modal':
                lib.libfive_tetfea_mark_modal_case(ptr, a.index)
                if not _density_given(material):
                    raise FeaError('a frequency needs a material with a density')
                continue
            for l in a.loads:
                if isinstance(l, _Force):
                    _add_force(ptr, l, a.index if several else None)
                elif isinstance(l, _Gravity) and not gravity_set:
                    lib.libfive_tetfea_set_gravity(ptr, *l.g, _number(material.density))
                    gravity_set = True
        if not lib.libfive_tetfea_prepare(ptr):
            raise FeaError(lib.libfive_tetfea_message(ptr).decode('utf-8', 'replace'))
        lib.libfive_tetfea_opt_clear(ptr)
        for key in program.measures:
            lib.libfive_tetfea_opt_measure(ptr, *program.kernel_measure(key))
        for code, value, index in program.ops:
            lib.libfive_tetfea_opt_op(ptr, int(code), float(value), int(index))
        lib.libfive_tetfea_opt_start(ptr, float(start), float(_number(material.density)) if not isinstance(material.density, Shape) else 0.0,
                                     (outputs[0].text or 'constraints').encode('utf-8'), -1.0 if goal == 'maximize' else 1.0)
        return ptr, handle

    space, slo, shi = space_of()
    ptr, handle = build(space, slo, shi)

    # cache key: the prepared problem, the settings, and where the keep / avoid regions are
    key = [fn('hash')(ptr), tuple(sorted(settings.items())), program.key()]
    if keep or avoid:
        # (by their expressions: the mesh's elements are not on a grid to sample them on)
        try:
            key.append(tuple(shape_key(_shape(r, 'keep')) for r in keep))
            key.append(tuple(shape_key(_shape(r, 'avoid')) for r in avoid))
        except Uncacheable:
            cache = False
    key = tuple(key)
    if cache and key in _topo_cache:
        cached = _topo_cache[key]
        _topo_cache.move_to_end(key)
        if cached.part is part:
            return cached
        r = object.__new__(TopologyResult)
        r.__dict__.update(cached.__dict__)
        r.part = part
        return r

    keep_arr = (ctypes.c_void_p * max(1, len(keep)))(*[_shape(r, 'keep').ptr for r in keep])
    avoid_arr = (ctypes.c_void_p * max(1, len(avoid)))(*[_shape(r, 'avoid').ptr for r in avoid])
    base_salt = result_cache.salt(kind, ekey) if ekey is not None else None

    def salted(p, number):
        ''' (each design space's problem has serials of its own: the pictures of one are not those of another) '''
        if base_salt is not None:
            fn('set_salt')(p, (base_salt ^ (number * 0x9E3779B97F4A7C15)) & 0xFFFFFFFFFFFFFFFF)

    salted(ptr, 0)
    t0 = time.time()

    def measure_meta(result):
        return {'bases': result._bases, 'outputs': result._outs, 'share': result.volume_fraction, 'history': list(result.history),
                'bounds': [[float(v) for v in result.bounds[0]], [float(v) for v in result.bounds[1]]], 'fenced': bool(getattr(result, 'fenced', False))}

    def keep_result(result, final):
        result_cache.save(kind, ekey, final, 'topology optimization', {'seconds': result.seconds, 'symmetry': result._symmetry_info,
                                                                        'measures': measure_meta(result)})
        if cache:
            _topo_cache[key] = result
        if ekey is not None:
            _topo_cache[ekey] = result
        while len(_topo_cache) > 8:
            _topo_cache.popitem(last=False)

    def run(p, iterations_now):
        ok = fn('optimize')(p, settings['penalty'], settings['filter_radius'], int(iterations_now), keep_arr, len(keep), avoid_arr, len(avoid),
                            int(max_iterations), float(tolerance), settings['extrude'])
        if not ok:
            raise FeaError(fn('message')(p).decode('utf-8', 'replace'))

    def resume_of(p):
        ''' What a run that stopped at the edge of its design space goes on from (the iteration, the sharpening's stage, the step ...); empty when it
            ran to its end '''
        buf = (ctypes.c_double * 16)()
        n = int(fn('resume')(p, buf, 16))
        return [buf[i] for i in range(min(n, 16))]

    earlier = []            # (pointer, handle) of the design spaces before the one that is being worked in
    fenced = False          # (the iterations ran out with the design still at the edge of its design space)
    made = 0                # (how many design spaces were made again)
    try:
        # The design may grow: it runs in a design space that is the part and a skin round it, and the moment material reaches the edge of that space
        # (outside the part) the run stops, the space is made again -- the part, its skin and everything within `reach` of the material -- and the SAME
        # run goes on in it, with the design, the sharpening, the step and the iteration it had.  So the design is never held against the edge of its
        # space while it runs, a design that does not reach the edge is done in the first space (it costs nothing more), and the design space costs what
        # the material costs.  Nothing stops it growing but the iterations, and the limit when one is given; the edge is not moved where the hole of a
        # support or a load is (nothing is added over their faces) or at the limit
        if free:
            fn('set_stop_at_edge')(ptr, 1)
        run(ptr, iterations)
        while free:
            state = resume_of(ptr)
            if not state:
                break
            design = design_surface(ptr)
            made += 1
            space2, slo2, shi2 = space_of(design) if design is not None else (space, slo, shi)
            ptr2, handle2 = build(space2, slo2, shi2)
            salted(ptr2, made)
            start_field = field_shape(fn('design')(ptr))        # (the trees stay alive until the run is over: the kernel is handed their handles)
            old_space = space
            lib.libfive_tetfea_set_start_field(ptr2, start_field.ptr)
            lib.libfive_tetfea_set_start_domain(ptr2, old_space.ptr)
            carried = (ctypes.c_double * len(state))(*state)
            fn('set_resume')(ptr2, carried, len(state))
            fn('set_stop_at_edge')(ptr2, 1 if design is not None else 0)
            earlier.append((ptr, handle))
            space, slo, shi, ptr, handle = space2, slo2, shi2, ptr2, handle2
            run(ptr, iterations)
            del start_field, old_space
        if free and fn('frontier')(ptr) > 0.1:
            fenced = True
            print('topology_optimization: the iterations (%d) ran out while the design still reached the edge of its design space: it would grow '
                  'further, and the part is cut at the edge of the space it was found in.  Give it more iterations, or run it again from this part '
                  '(the design it made is the better start), or say how far it may go with grow_limit.' % int(iterations))
    except BaseException:
        # An edit of the script stopped this run.  The stop reaches Python only as the solver returns -- so a solution that is whole is
        # there, and is kept for the run of the edited text (the same problem, unless the edit changed it): minutes of work must not be
        # thrown away, to be done again, by a click on an eye.  (A solver that was cancelled has a message, and is not kept; nor is a run
        # that stopped at the edge of its space, which is not the end of the design.)
        try:
            hist = (ctypes.c_double * 1000)()
            if not fn('message')(ptr) and not resume_of(ptr) and fn('history')(ptr, hist, 1000) > 0:
                keep_result(finish(handle, ptr, time.time() - t0, earlier=earlier, space=(space, slo, shi), fenced=fenced), ptr)
        except Exception:
            pass
        raise
    result = finish(handle, ptr, time.time() - t0, earlier=earlier, space=(space, slo, shi), fenced=fenced)
    keep_result(result, ptr)
    return result


# (the final design of an optimisation is its field; the fields it calculated are listed: see FieldResult)
TopologyResult._as_field = lambda self: self.shape()
TopologyResult.fields = property(lambda self: OrderedDict([('shape', self.shape()), ('density', self.density)]))


# It is given the result of an analysis (a simulation) and writes `constraints=` as a placeholder: what the design is for is the user's
from fieldes.kinds import takes as _declare
topology_optimization._takes = _declare({'simulation'}, [{'simulation'}])
topology_optimization._required = ('constraints',)
