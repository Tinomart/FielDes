'''
Static finite element analysis (linear elasticity) of FielDes shapes.

    from fieldes import *

    bracket = ...                                        # any Shape, in mm
    conditions = static_boundary_conditions(
        bracket,
        supports=[fixed(box((0, 0, 0), (5, 40, 20)))],   # clamp the left end
        loads=[force(box((95, 0, 0), (100, 40, 20)), (0, 0, -200))])   # 200 N down
    conditions                                           # shown on the part: held (blue), pushed (red)
    result = static_analysis(bracket, conditions, material=aluminium, element_size=1.0)
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
from collections import OrderedDict

from fieldes.ffi import lib, libfive_region_t
from fieldes.shape import Shape
from fieldes.stdlib.excluded import keep_regions, carry_locks
from fieldes.stdlib.content_cache import Uncacheable, cache_for, problem_key, shape_key, value_key
from fieldes.stdlib import result_cache

# The element a cell of the voxel grid is made of (Element in fea.hpp)
ELEMENTS = {'tet': 0, 'hex': 1, 'hex_basic': 2}


def _set_element(ptr, element):
    if element not in ELEMENTS:
        raise ValueError("element is 'tet' (six tetrahedra a cell, the default), 'hex' "
                         "(hexahedra with incompatible modes) or 'hex_basic' (plain hexahedra)")
    if getattr(lib, 'libfive_fea_set_element', None) is None:
        if element != 'hex':
            raise FeaError('this FielDes library is too old for the {!r} element'.format(element))
        return
    lib.libfive_fea_set_element(ptr, ELEMENTS[element])

__all__ = ['Material', 'steel', 'stainless_steel', 'aluminium', 'titanium',
           'pla', 'petg', 'abs_plastic', 'nylon',
           'fixed', 'force', 'gravity', 'thermal_expansion', 'static_analysis', 'colored',
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


class _Support:
    def __init__(self, region, x, y, z):
        self.region, self.axes = region, (x, y, z)


class _Force:
    def __init__(self, region, vector, profile=None):
        self.region, self.vector, self.profile = region, vector, profile


class _Gravity:
    def __init__(self, g):
        self.g = g


class _Thermal:
    def __init__(self, temperature, reference):
        self.temperature, self.reference = temperature, reference


def _shape(x, what):
    if not isinstance(x, Shape):
        raise TypeError('{} must be a Shape (a region), not {!r}'.format(what, type(x).__name__))
    return x


class _PlainConditions:
    ''' The supports and loads of an analysis made inside the library (what static_boundary_conditions() gives,
        without the picture) '''
    static_conditions = True

    def __init__(self, supports, loads):
        self.supports, self.loads = list(supports), loads


def _conditions(conditions, what):
    ''' (supports, loads) of the boundary conditions an analysis is given -- the one way to give them:

            conditions = static_boundary_conditions(part, supports=[fixed(base)], loads=[force(lug, (0, -2000, 0))])
            result = static_analysis(part, conditions, material=aluminium)
        '''
    if not getattr(conditions, 'static_conditions', False):
        raise TypeError(
            '{0}: the supports and loads are boundary conditions that are given to it, no longer lists of '
            'fixed(...) and force(...) items: {0}(part, static_boundary_conditions(part, supports=[fixed(...)], '
            'loads=[force(...)]), ...) -- and show them with `conditions` on a line of its own'.format(what))
    return list(conditions.supports), conditions.loads


def fixed(region, x=True, y=True, z=True):
    ''' A support: the part is held in place wherever it lies inside
        `region` (a Shape).  x / y / z = False leave that direction free
        (a sliding support). '''
    return _Support(_shape(region, 'fixed(region)'), bool(x), bool(y), bool(z))


def force(region, fx, fy=None, fz=None, profile=None):
    ''' A load: the total force (fx, fy, fz) in N, spread evenly over the
        part's surface inside `region` (a Shape).  force(region, (0, 0, -100))
        works too.

        profile  a field: the total is spread over the surface in proportion to it (not negative) instead of evenly --
                 a pressure that is not the same everywhere, e.g. `profile=ramp(x_field(), (0, 80), (0.2, 1.0))` loads
                 the end of a beam five times harder at x = 80 than at x = 0, with the same total.  (Tetrahedral
                 elements.) '''
    if fy is None and fz is None:
        fx, fy, fz = fx
    if any(isinstance(v, Shape) for v in (fx, fy, fz)):
        raise TypeError('force(): the components of a force are numbers (N); to spread it unevenly over the region give '
                        'profile=<a field>')
    if profile is not None and not isinstance(profile, Shape):
        raise TypeError('force(): profile is a field (a Shape)')
    return _Force(_shape(region, 'force(region)'), (float(fx), float(fy), float(fz)), profile)


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
    if element != 'tet':
        used = _fields_in_use(material, loads)
        if used:
            raise FeaError("{}: {} {} a field -- fields in a material and in loads work with the tetrahedral elements "
                           "(element='tet', the default), not {!r}".format(what, ', '.join(used),
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
_MAX_DRAWN_VALUES = 1500000

_FIELDS = ['von_mises', 'displacement', 'ux', 'uy', 'uz',
           'sxx', 'syy', 'szz', 'sxy', 'syz', 'szx',
           'max_principal', 'min_principal', 'strain_energy']
_LABELS = {'von_mises': 'von Mises stress (MPa)', 'displacement': 'displacement (mm)',
           'ux': 'displacement x (mm)', 'uy': 'displacement y (mm)', 'uz': 'displacement z (mm)',
           'sxx': 'stress xx (MPa)', 'syy': 'stress yy (MPa)', 'szz': 'stress zz (MPa)',
           'sxy': 'stress xy (MPa)', 'syz': 'stress yz (MPa)', 'szx': 'stress zx (MPa)',
           'max_principal': 'max principal stress (MPa)',
           'min_principal': 'min principal stress (MPa)',
           'strain_energy': 'strain energy density (mJ/mm^3)'}


class _Handle:
    ''' Owns a libfive_fea object (freed with the last result using it) '''
    def __init__(self, ptr):
        self.ptr = ptr

    def __del__(self):
        if self.ptr:
            lib.libfive_fea_delete(self.ptr)
            self.ptr = None


class Result:
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
            setattr(self, name, Shape(ptr))
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
                    steps.append({'label': 'load 100 %'})
                else:
                    steps.append({'label': 'load %d %%' % (5 * k),
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
            return '{:,} tetrahedra'.format(self.elements)
        return '{:,} hexahedra'.format(self.elements)

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
        text = ('Static analysis: {} ({:.1f} s)\n'
                '  von Mises max {:.4g} MPa \u00b7 displacement max {:.3g} mm').format(
                    self.element_text(), self.seconds, self.max_von_mises, self.max_displacement)
        if self.safety_factor is not None:
            text += '\n  safety factor {:.3g} ({})'.format(self.safety_factor, self.material.name)
        if self.loose_elements:
            text += '\n  {} loose elements (connected to no support) left out'.format(self.loose_elements)
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


def static_analysis(shape, conditions, material=steel, element_size=None,
                    bounds=None, max_iterations=20000, tolerance=1e-6, cache=True, element='tet'):
    ''' Linear static analysis of `shape` (a Shape, in mm).

        conditions the boundary conditions: static_boundary_conditions(part, supports, loads) -- the
                   supports are fixed(...) items (at least one), the loads force(...) / gravity(...) /
                   thermal_expansion(...) items.  They are a shape of their own, drawn on the part
        material   a Material (default steel); E in MPa
        element_size   the element edge in mm (default: the part's longest
                   side over 60)
        element    'tet' (default: tetrahedra that follow the part's surface),
                   'hex' or 'hex_basic' (a voxel grid): see the module
                   documentation
        bounds     region to analyse, ((x0, y0, z0), (x1, y1, z1)); found
                   automatically if not given

        Returns a Result (see its fields).  Raises FeaError if the problem
        can't be solved as given (no support touching the part, supports
        that don't hold it in place, ...). '''
    if not isinstance(shape, Shape):
        raise TypeError('static_analysis: the part must be a Shape')
    supports, loads = _conditions(conditions, 'static_analysis')
    if _is_cases(loads):
        raise FeaError('a static analysis has one set of loads: load cases (a list of lists) are for '
                       'topology_optimization')
    loads = list(loads if isinstance(loads, (list, tuple)) else [loads])
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
        element_size = max(size) / 60.0
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
    if element == 'tet':
        return _static_analysis_tet(shape, supports, loads, material, element_size, region, max(size),
                                    max_iterations, tolerance, cache, ekey)

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
        lib.libfive_fea_add_support(ptr, s.region.ptr, *[int(a) for a in s.axes])
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
        own (constant in it), and the fields are read anywhere in the mesh.  Everything
        of Result: fields as shapes, .range(), .element_range(), ... '''

    def __init__(self, handle, shape, material, element_size, size):
        self._handle = handle
        self.shape = shape
        self.material = material
        self.element_size = element_size
        self.element = 'tet'
        self._size = float(size)
        p = handle.ptr
        self._ranges = {}
        for i, name in enumerate(_FIELDS):
            setattr(self, name, Shape(lib.libfive_tetfea_field(p, i)))
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
        self.max_von_mises = self._element_ranges.get('von_mises', self._ranges['von_mises'])[1]
        self.max_displacement = self._ranges['displacement'][1]
        ys = material.yield_strength
        self.safety_factor = (ys / self.max_von_mises) if ys and self.max_von_mises > 0 else None

    def _model_size(self):
        return self._size

    def element_text(self):
        return '{:,} tetrahedra \u00b7 {:,} nodes'.format(self.elements, self.nodes)

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
        return out


def _static_analysis_tet(shape, supports, loads, material, element_size, region, size, max_iterations,
                         tolerance, cache, ekey=None):
    ''' static_analysis on a body-fitted tetrahedral mesh '''
    if getattr(lib, 'libfive_tetfea_new', None) is None:
        raise FeaError("this FielDes library is too old for tetrahedral meshing")
    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load('tetfea', ekey, 'static analysis')
    if loaded is not None:
        return TetResult(_TetHandle(loaded[0]), shape, material, element_size, size)
    # (a Young's modulus that is a field is measured against 1 MPa: each element has its own value of it)
    ptr = lib.libfive_tetfea_new(shape.ptr, region, element_size, _number(material.E, 1.0), material.nu)
    handle = _TetHandle(ptr)
    _material_fields(ptr, material)
    for s in supports:
        if not isinstance(s, _Support):
            raise TypeError('supports must be fixed(...) items')
        lib.libfive_tetfea_add_support(ptr, s.region.ptr, *[int(a) for a in s.axes])
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
    key = ('tet', lib.libfive_tetfea_hash(ptr))
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
    result = TetResult(handle, shape, material, element_size, size)
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
            setattr(self, name, Shape(lib.libfive_fea_mode_field(p, index, f)))
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
                steps.append({'label': 'phase %d\u00b0' % (360 * k // n),
                              'deform': (s * self.ux, s * self.uy, s * self.uz)})
            auto = 0.05 * self._model_size()
            shown = {'_color_fields': [(name, 'mode %d %s' % (self.index + 1, name), getattr(self, name)) +
                                       tuple(self._ranges[name]) for name in ('displacement', 'ux', 'uy', 'uz')],
                     '_color_field_name': 'displacement', '_deform': (self.ux, self.uy, self.uz),
                     '_deform_auto': auto, '_deform_scale': auto,
                     '_color_steps': steps, '_color_step': n // 4}
            self._shown = shown
        label = 'mode %d, %.4g Hz (displacement)' % (self.index + 1, self.frequency)
        out = colored(self.shape, self.displacement, range=self._ranges['displacement'], label=label)
        out.__dict__.update(shown)
        out._color_detail = float(getattr(self, 'element_size', 0) or 0)
        return out

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
            setattr(self, name, Shape(lib.libfive_tetfea_mode_field(p, index, f)))
            self._ranges[name] = (lib.libfive_tetfea_mode_field_min(p, index, f),
                                  lib.libfive_tetfea_mode_field_max(p, index, f))

    def _model_size(self):
        return self._size


class ModalResult:
    ''' The result of modal_analysis(): .frequencies (Hz, lowest first),
        .modes (a Mode each: its shape as fields), .seconds.  Stated on its
        own it shows the first mode (result.modes[1] the second, ...). '''

    def __init__(self, handle, shape, material, element_size, seconds, size=None):
        p = handle.ptr
        self.material = material
        self.seconds = seconds
        if size is not None:
            # (on a tetrahedral mesh)
            n = lib.libfive_tetfea_mode_count(p)
            self.modes = [TetMode(handle, i, lib.libfive_tetfea_mode_frequency(p, i), shape, size, element_size)
                          for i in range(n)]
        else:
            grid = (int(lib.libfive_fea_stat(p, 16)), int(lib.libfive_fea_stat(p, 17)),
                    int(lib.libfive_fea_stat(p, 18)))
            n = lib.libfive_fea_mode_count(p)
            self.modes = [Mode(handle, i, lib.libfive_fea_mode_frequency(p, i), shape, element_size, grid)
                          for i in range(n)]
        self.frequencies = [m.frequency for m in self.modes]

    def __getitem__(self, i):
        return self.modes[i]

    def __len__(self):
        return len(self.modes)

    def _display(self):
        return self.modes[0]._display()

    def __repr__(self):
        return 'Modal analysis: %s Hz (%s, %.1f s)' % (
            ' \u00b7 '.join('%.4g' % f for f in self.frequencies), self.material.name, self.seconds)


_modal_cache = OrderedDict()


def modal_analysis(shape, conditions, material=steel, modes=6, element_size=None, bounds=None,
                   max_iterations=100, tolerance=1e-6, cache=True, element='tet'):
    ''' The natural frequencies and mode shapes of `shape` (a Shape, in
        mm), held by the supports of the boundary conditions
        (static_boundary_conditions(part, supports=[fixed(...)]) -- how it
        vibrates.  No loads are needed (any given are not used); the
        material's E and density are used.

        modes        how many (the lowest first)
        element_size mm (default: about 40 elements along the longest side)
        element      'tet' (default), 'hex' or 'hex_basic': see the module
                     documentation

        Returns a ModalResult: .frequencies (Hz), .modes[i] (the shape as
        fields -- displacement, ux, uy, uz).  The shapes are fields like any
        other: e.g. stiffen the part where the first mode moves most.  An
        unchanged problem is cached. '''
    if not isinstance(shape, Shape):
        raise TypeError('modal_analysis: the part must be a Shape')
    if not _density_given(material):
        raise FeaError('modal_analysis needs a material with a density')
    _refuse_fields(material, (), element, 'modal_analysis')
    supports = _conditions(conditions, 'modal_analysis')[0]
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
        element_size = max(size) / 40.0
    element_size = float(element_size)
    pad = 1e-6 * max(max(size), 1e-9)
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i] - pad, hi[i] + pad
    tet = element == 'tet'
    if tet and getattr(lib, 'libfive_tetfea_new', None) is None:
        raise FeaError("this FielDes library is too old for tetrahedral meshing")
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
        _material_fields(ptr, material)
    else:
        ptr = lib.libfive_fea_new(shape.ptr, region, element_size, material.E, material.nu)
        handle = _Handle(ptr)
        _set_element(ptr, element)
    for s in supports:
        if not isinstance(s, _Support):
            raise TypeError('supports must be fixed(...) items')
        (lib.libfive_tetfea_add_support if tet else lib.libfive_fea_add_support)(
            ptr, s.region.ptr, *[int(a) for a in s.axes])
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
    return out


################################################################################
# Topology optimization

class TopologyResult:
    ''' The result of topology_optimization():
        .density     a field, 0 (no material) .. 1 (solid), in the part
        .shape()     the optimized part, keeping the volume fraction asked
                     for (shape(threshold=0.5): where density > 0.5)
        .compliance  compliance (N mm) at each iteration -- lower is stiffer
        .densities   the density field after each iteration (tetrahedral
                     optimizations)
        .volume_fraction, .iterations, .seconds
        .pieces      how many separate pieces the optimized part is in (tetrahedral optimizations; more than
                     one is warned about when the result is made: try a higher volume_fraction)
        .verify()    a static analysis of the optimized part (stresses) --
                     a list, one per load case, when there are several
        Stated on its own, FielDes shows the optimized part coloured by the
        density, and the result card steps through the iterations: the part
        as it was after each one (slider, play / pause). '''

    def __init__(self, handle, part, density, history, settings, element_size, bounds,
                 supports, loads, material, stats):
        self._handle = handle
        self.part = part
        self.density = density
        self.compliance = history
        self.settings = settings
        self.element_size = element_size
        self.bounds = bounds
        self._supports, self._loads, self.material = supports, loads, material
        self.volume_fraction = stats['volume_fraction']
        self.iterations = stats['iterations']
        self.seconds = stats['seconds']

    def keep_threshold(self, samples=40):
        ''' The density above which the part keeps the volume fraction that
            was asked for.  (The density field is partly grey -- between 0
            and 1 -- so cutting at 0.5 kept only 9-15 % of a block asked to
            keep 25 %: a much weaker part than the optimised one.) '''
        if getattr(self, '_keep_threshold', None) is not None:
            return self._keep_threshold
        from fieldes.stdlib.fields import sample_grid
        lo, hi = self.bounds
        _, part = sample_grid(self.part, lo, hi, samples)
        _, dens = sample_grid(self.density, lo, hi, samples)
        inside = [d for pv, d in zip(part, dens) if pv < 0]
        t = 0.5
        if inside:
            target = self.settings['volume_fraction'] * len(inside)
            a, b = 0.0, 1.0
            for _ in range(40):
                t = 0.5 * (a + b)
                kept = sum(1 for d in inside if d > t)
                if kept > target:
                    a = t
                else:
                    b = t
        self._keep_threshold = t
        return t

    def shape(self, threshold=None):
        ''' The optimized part: the original part where the density is above
            `threshold` (a smooth surface through the density field).  By
            default the threshold that keeps the volume fraction asked for
            (see keep_threshold); 0.5 cuts the density field in the middle. '''
        if threshold is None:
            threshold = self.keep_threshold()
        # the density rises from 0 to 1 over about two filter widths: scale
        # it to roughly mm near the threshold so offsets behave
        width = 2.0 * (self.settings['filter_radius'] or 1.5 * self.element_size)
        out = ((threshold - self.density) * width).max(self.part)
        out._bounds = self.bounds
        return carry_locks(out, self.part)

    def verify(self, threshold=None, element_size=None):
        ''' A static analysis of the optimized part with the same supports,
            loads and material -- with several load cases, a list of them,
            one per case '''
        cases = self._loads if _is_cases(self._loads) else [self._loads]
        out = [static_analysis(self.shape(threshold), _PlainConditions(self._supports, list(case)), self.material,
                               element_size=element_size or self.element_size, bounds=self.bounds,
                               element=self.settings.get('element', 'hex'))
               for case in cases]
        return out if len(out) > 1 else out[0]

    def _display(self):
        ''' What FielDes shows for the result stated on its own: the optimized part (see shape())
            coloured by the density, with a step per iteration -- the part as it was then '''
        shown = getattr(self, '_shown', None)
        if shown is None:
            threshold = self.keep_threshold()
            width = 2.0 * (self.settings['filter_radius'] or 1.5 * self.element_size)
            densities = getattr(self, 'densities', None) or []
            steps = []
            for k, d in enumerate(densities):
                step = {'label': 'iteration %d of %d' % (k + 1, len(densities)),
                        'channels': [('density', 'density', d, 0.0, 1.0)]}
                if k + 1 < len(densities):
                    body = ((threshold - d) * width).max(self.part)
                    body._bounds = self.bounds
                    step['shape'] = body
                steps.append(step)
            shown = {'_color_fields': [('density', 'density', self.density, 0.0, 1.0)],
                     '_color_field_name': 'density', '_shape': self.shape(threshold)}
            if steps:
                shown['_color_steps'] = steps
                shown['_color_step'] = len(steps) - 1
            self._shown = shown
        out = colored(shown['_shape'], self.density, range=(0.0, 1.0), label='density')
        out.__dict__.update({k: v for k, v in shown.items() if k != '_shape'})
        out._color_detail = float(getattr(self, 'element_size', 0) or 0)
        return out

    def __repr__(self):
        c = self.compliance
        return ('Topology optimization: {:.0f} % of the part kept, compliance {:.4g} -> {:.4g} N mm '
                '({} iterations, {:.1f} s)').format(100 * self.volume_fraction,
                                                  c[0] if c else 0, c[-1] if c else 0,
                                                  self.iterations, self.seconds)


_topo_cache = OrderedDict()


def _is_cases(loads):
    ''' loads given as load cases: a list of lists of loads '''
    return (isinstance(loads, (list, tuple)) and len(loads) > 0 and
            all(isinstance(c, (list, tuple)) for c in loads))


def topology_optimization(part, conditions, material=steel, volume_fraction=0.3,
                          element_size=None, iterations=60, filter_radius=None, keep=None,
                          avoid=None, extrude=None, penalty=3.0, move=0.2, bounds=None,
                          max_iterations=20000, tolerance=1e-5, cache=True, element='tet'):
    ''' Topology optimization: the stiffest part that uses `volume_fraction`
        of the material of `part` (the design space), for the supports and
        loads of the boundary conditions (as in static_analysis).

        keep:   regions (Shapes, or a list) that must stay solid -- e.g. bolt
                bosses, mounting faces; the material around supports and
                loads always stays
        avoid:  regions that must stay empty
        extrude: 'x', 'y' or 'z' -- the design is the same all along that
                axis (outside the keep / avoid regions): a profile to
                extrude, or to cut right through from one side
        element_size:  mm (default: 40 elements along the longest side; the
                optimization solves the analysis ~30-60 times)
        filter_radius: the smallest member size scale, mm (default 1.5
                elements)
        element: 'tet' (default: tetrahedra that follow the part's surface, a density in each),
                'hex' or 'hex_basic' (a regular grid of hexahedra)

        conditions: static_boundary_conditions(part, supports, loads): the loads
                a list of loads -- or several load cases, a list of such
                lists: loads=[[force(a, ...)], [force(b, ...), gravity()]].
                Each case acts on its own and the part is made stiff for all
                of them (the sum of their compliances is minimised) -- e.g. a
                bracket pushed down in use and sideways in assembly.

        Returns a TopologyResult: .density (a field), .shape() (the
        optimized part), .compliance, .verify().  An unchanged problem is
        cached, so re-running a script is instant. '''
    if not isinstance(part, Shape):
        raise TypeError('topology_optimization: the part must be a Shape')
    if getattr(lib, 'libfive_fea_optimize', None) is None:
        raise FeaError('this FielDes library is too old for topology optimization')
    supports, loads = _conditions(conditions, 'topology_optimization')
    if _is_cases(loads):
        cases = [list(c) for c in loads]
    else:
        cases = [list(loads if isinstance(loads, (list, tuple)) else [loads])]
    for i, case in enumerate(cases):
        if not case:
            raise FeaError('load case {} has no loads'.format(i + 1))
    loads = cases if len(cases) > 1 else cases[0]
    keep = [] if keep is None else list(keep if isinstance(keep, (list, tuple)) else [keep])
    avoid = [] if avoid is None else list(avoid if isinstance(avoid, (list, tuple)) else [avoid])
    # (what the part has excluded stays as it is: its locked fields are regions to keep)
    keep = keep + keep_regions(part)
    # The whole problem by its content, before anything is built (see static_analysis)
    ekey = problem_key('topology', part=part, part_bounds=getattr(part, '_bounds', None),
                       supports=supports, loads=loads, material=material,
                       volume_fraction=float(volume_fraction), element_size=element_size,
                       iterations=int(iterations), filter_radius=filter_radius, keep=keep, avoid=avoid,
                       extrude=extrude, penalty=float(penalty), move=float(move), bounds=bounds,
                       max_iterations=max_iterations, tolerance=tolerance,
                       element=element) if cache else None
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
    pad = 1e-6 * max(max(size), 1e-9)
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i] - pad, hi[i] + pad
    tet = element == 'tet'
    api = 'libfive_tetfea_' if tet else 'libfive_fea_'
    fn = lambda name: getattr(lib, api + name)
    kind = 'tetfea' if tet else 'fea'
    axes = {None: -1, 'x': 0, 'y': 1, 'z': 2}
    if extrude not in axes:
        raise ValueError("extrude is None, 'x', 'y' or 'z'")
    settings = {'volume_fraction': float(volume_fraction), 'penalty': float(penalty),
                'filter_radius': float(filter_radius or 0.0), 'iterations': int(iterations),
                'move': float(move), 'extrude': axes[extrude], 'element': element}

    def finish(handle, ptr, seconds):
        ''' The result from a solved problem (just optimised, or read back from its file) '''
        density = Shape(fn('density')(ptr))
        hist = (ctypes.c_double * 1000)()
        m = fn('history')(ptr, hist, 1000)
        history = [hist[i] for i in range(min(m, 1000))]
        # the density after every iteration (on the mesh; the voxel optimizer keeps only the last)
        densities = []
        at = getattr(lib, 'libfive_tetfea_density_at', None) if tet else None
        for k in range(len(history)):
            p = at(ptr, k) if at else None
            if not p:
                break
            densities.append(Shape(p))
        stats = {'volume_fraction': settings['volume_fraction'], 'iterations': len(history), 'seconds': seconds}
        result = TopologyResult(handle, part, density, history, settings, element_size, (lo, hi),
                                supports, loads, material, stats)
        result.densities = densities
        # A part that falls into pieces cannot do what a part is for: say so, and what to try.  (The scraps are left
        # in the result on purpose: they show what the part would become with more volume.)
        count = getattr(lib, 'libfive_tetfea_pieces', None) if tet else None
        if count is not None:
            # (a link thinner than a millimetre of the cut surface is not one: see shape(), the density is scaled to mm)
            width = 2.0 * (settings['filter_radius'] or 1.5 * element_size)
            result.pieces = int(count(ptr, result.keep_threshold(), 1.0 / width))
            if result.pieces > 1:
                print('topology_optimization: the optimised part is in %d separate pieces (nothing joins them). '
                      'Try a higher volume_fraction (it is %g now) -- the loose scraps show what the part would '
                      'become -- or fewer or smaller keep regions.' % (result.pieces, settings['volume_fraction']))
        return result

    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load(kind, ekey, 'topology optimization')
    if loaded is not None:
        result = finish(_TetHandle(loaded[0]) if tet else _Handle(loaded[0]), loaded[0], float(loaded[1].get('seconds', 0)))
        _topo_cache[ekey] = result
        return result
    if tet:
        if getattr(lib, 'libfive_tetfea_optimize', None) is None:
            raise FeaError('this FielDes library is too old for tetrahedral topology optimization')
        ptr = lib.libfive_tetfea_new(part.ptr, region, element_size, _number(material.E, 1.0), material.nu)
        handle = _TetHandle(ptr)
        _material_fields(ptr, material)
    else:
        _refuse_fields(material, [l for case in cases for l in case], element, 'topology_optimization')
        ptr = lib.libfive_fea_new(part.ptr, region, element_size, material.E, material.nu)
        handle = _Handle(ptr)
        _set_element(ptr, element)
    fn = lambda name: getattr(lib, api + name)
    for s in supports:
        if not isinstance(s, _Support):
            raise TypeError('supports must be fixed(...) items')
        fn('add_support')(ptr, s.region.ptr, *[int(a) for a in s.axes])
    gravity_set = False
    for case_index, case in enumerate(cases):
        for l in case:
            if isinstance(l, _Force):
                if tet:
                    _add_force(ptr, l, case_index if len(cases) > 1 else None)
                elif len(cases) > 1:
                    fn('add_force_case')(ptr, l.region.ptr, *l.vector, case_index)
                else:
                    fn('add_force')(ptr, l.region.ptr, *l.vector)
            elif isinstance(l, _Thermal):
                raise FeaError("thermal_expansion isn't supported in topology optimization (its "
                               "load changes with the design)")
            elif isinstance(l, _Gravity):
                # (gravity acts in every case)
                if not _density_given(material):
                    raise FeaError('gravity needs a material with a density')
                if not gravity_set:
                    fn('set_gravity')(ptr, *l.g, _number(material.density))
                    gravity_set = True
            else:
                raise TypeError('loads must be force(...) or gravity(...) items')
    if not fn('prepare')(ptr):
        raise FeaError(fn('message')(ptr).decode('utf-8', 'replace'))

    # cache key: the prepared problem, the settings, and where the keep /
    # avoid regions are (sampled at the element centres)
    key = [fn('hash')(ptr), tuple(sorted(settings.items()))]
    if tet and (keep or avoid):
        # (by their expressions: the mesh's elements are not on a grid to sample them on)
        try:
            key.append(tuple(shape_key(_shape(r, 'keep')) for r in keep))
            key.append(tuple(shape_key(_shape(r, 'avoid')) for r in avoid))
        except Uncacheable:
            cache = False
    elif keep or avoid:
        glo = (ctypes.c_double * 3)()
        gh = ctypes.c_double()
        gd = (ctypes.c_int * 3)()
        lib.libfive_fea_grid(ptr, glo, ctypes.byref(gh), gd)
        n = gd[0] * gd[1] * gd[2]
        xyz = (ctypes.c_float * (3 * n))()
        q = 0
        for k in range(gd[2]):
            for j in range(gd[1]):
                for i in range(gd[0]):
                    xyz[q] = glo[0] + (i + 0.5) * gh.value
                    xyz[q + 1] = glo[1] + (j + 0.5) * gh.value
                    xyz[q + 2] = glo[2] + (k + 0.5) * gh.value
                    q += 3
        out = (ctypes.c_float * n)()
        for group in (keep, avoid):
            bits = bytearray(n)
            for r in group:
                lib.libfive_tree_eval_points(_shape(r, 'keep / avoid').ptr, xyz, n, out)
                for i in range(n):
                    if out[i] < 0:
                        bits[i] = 1
            key.append(hash(bytes(bits)))
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
    if ekey is not None:
        fn('set_salt')(ptr, result_cache.salt(kind, ekey))
    t0 = time.time()
    ok = fn('optimize')(ptr, settings['volume_fraction'], settings['penalty'],
                        settings['filter_radius'], settings['iterations'],
                        settings['move'], keep_arr, len(keep), avoid_arr, len(avoid),
                        int(max_iterations), float(tolerance), settings['extrude'])
    if not ok:
        raise FeaError(fn('message')(ptr).decode('utf-8', 'replace'))
    result = finish(handle, ptr, time.time() - t0)
    result_cache.save(kind, ekey, ptr, 'topology optimization', {'seconds': result.seconds})
    if cache:
        _topo_cache[key] = result
    if ekey is not None:
        _topo_cache[ekey] = result
    while len(_topo_cache) > 8:
        _topo_cache.popitem(last=False)
    return result
