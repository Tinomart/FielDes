'''
Steady-state thermal analysis (heat conduction) of FielDes shapes.

    from fieldes import *

    part = ...                                          # any Shape, in mm
    result = thermal_analysis(
        part,
        fixed_temperatures=[fixed_temperature(base_region, 20)],       # held at 20 degrees C
        heat_inputs=[heat_input(chip_region, 5.0)],                    # 5 W into the part here
        heat_generations=[],                                           # (none)
        convections=[convection(fins_region, 25e-6, ambient=20)],      # air cooling
        material=aluminium, element_size=1.0)
    result                               # the part coloured by temperature (FielDes)
    thicker = part - 0.2 * result.heat_flux   # results are fields like any other

    # the material layout (30 % of the part) that keeps the part coolest, for the conditions of the analysis
    design = topology_optimization(result, constraints=[minimize('max_temperature'), 'volume_fraction <= 0.3'])
    design.shape()

Every kind of condition is an input of its own, needed -- written with a placeholder -- and takes one item or a list ([] says there is none).
Each condition is a model of its own, with an eye: it is drawn on the part the analysis was given.

Boundary conditions are regions -- ordinary shapes, as for static_analysis:
  fixed_temperature(region, T)   the part is held at T inside the region
  heat_input(region, watts)      a total power spread over the part's surface
                                 inside the region
  heat_generation(region, watts) a total power generated in the part's volume
                                 inside the region (e.g. a resistive heater,
                                 electronics potted in the part)
  convection(region, h, ambient) the part's exposed surface inside the region
                                 exchanges heat with an ambient temperature;
                                 h in W / (mm^2 K): still air ~5e-6 - 25e-6,
                                 forced air ~25e-6 - 250e-6, water ~500e-6 -
                                 10000e-6 (1 W / (m^2 K) = 1e-6 W / (mm^2 K))
At least one fixed temperature or convection is needed (else the temperature
isn't determined).  Units: mm, W, degrees C (or K); the material's
conductivity in W / (mm K) (aluminium ~0.167 = 167 W / (m K)).

Uses the same elements as static_analysis: tetrahedra that follow the part's
surface (element='tet', the default: a temperature is linear in each and the
heat flux constant in it) or, with element='hex', a voxel grid of cubes (partly
filled at the surface), and a preconditioned conjugate-gradient solve.  Topology
optimization of a thermal analysis (topology_optimization) designs on the tetrahedral mesh.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import time
from collections import OrderedDict

from fieldes.ffi import lib, libfive_region_t
from fieldes.i18n import tr as _tr
from fieldes.shape import Shape, field_shape
from fieldes.stdlib.element_size import default_element_size
from fieldes.stdlib.excluded import keep_regions
from fieldes.stdlib.fea import (FeaError, FieldResult, TopologyResult, aluminium, colored, _bounds, _shape, _regions, _Condition, _register_slots, _given,
                                _use_conditions, _with_conditions)
from fieldes.stdlib.content_cache import Uncacheable, problem_key, shape_key, value_key
from fieldes.stdlib.element_mesh import element_mesh as _element_mesh, elements_note as _elements_note
from fieldes.stdlib import result_cache

__all__ = ['fixed_temperature', 'heat_input', 'heat_generation', 'convection',
           'thermal_analysis', 'ThermalResult', 'ThermalTopologyResult']


class _Temperature(_Condition):
    _call = 'fixed_temperature(...)'

    def __init__(self, region, value):
        self.region, self.value = region, value


class _Heat(_Condition):
    def __init__(self, region, power, volume=False, profile=None):
        self.region, self.power, self.volume, self.profile = region, power, volume, profile


class _HeatInput(_Heat):
    _call = 'heat_input(...)'


class _HeatGeneration(_Heat):
    _call = 'heat_generation(...)'


class _Convection(_Condition):
    _call = 'convection(...)'

    def __init__(self, region, coefficient, ambient):
        self.region, self.coefficient, self.ambient = region, coefficient, ambient


_register_slots({'fixed_temperatures': ((_Temperature,), 'fixed_temperature(...) items'),
                 'heat_inputs': ((_HeatInput,), 'heat_input(...) items'),
                 'heat_generations': ((_HeatGeneration,), 'heat_generation(...) items'),
                 'convections': ((_Convection,), 'convection(...) items')})


def _boundary(fixed_temperatures, heat_inputs, heat_generations, convections, what):
    ''' The conditions of a thermal analysis, one list: what each input is given '''
    return (_given(fixed_temperatures, 'fixed_temperatures', what) + _given(heat_inputs, 'heat_inputs', what) +
            _given(heat_generations, 'heat_generations', what) + _given(convections, 'convections', what))


def _number_or_field(value, what):
    ''' A condition's value: a number, or a field (a Shape: its value at each point of the surface it acts on) '''
    if isinstance(value, Shape):
        return value
    try:
        return float(value)
    except (TypeError, ValueError):
        raise TypeError('{} is a number or a field (a Shape), not {!r}'.format(what, type(value).__name__))


def _profile(profile, what):
    if profile is not None and not isinstance(profile, Shape):
        raise TypeError('{}: profile is a field (a Shape)'.format(what))
    return profile


def _one(rest, what, count=1):
    if len(rest) != count:
        raise TypeError('{}: after the regions it acts on (any number, one argument after the other) comes {}'.format(
            what, 'the value' if count == 1 else 'its values'))
    return rest


def fixed_temperature(*args, region=None, body=None, value=None):
    ''' The part held at temperature `value` wherever it lies inside
        `region` (a Shape; any number of regions can be given, one argument after the other, then the value:
        fixed_temperature(a, b, 20)).

        region  where the part is held: a body, a field or a selected surface (the same as the first argument)
        body    the body it is held on, when the regions are bodies that choose WHERE on it: `fixed_temperature(plate, 20, body=part)` holds the part's surface where it meets the plate
        value   the temperature (degrees C): a number, or a field -- the temperature at each point (tetrahedral elements) '''
    found, rest = _regions(args, region, 'fixed_temperature()', body)
    if value is not None:
        if rest:
            raise TypeError('fixed_temperature(): the value is given once -- as the argument after the regions, or as value=')
        rest = (value,)
    value, = _one(rest, 'fixed_temperature(region, ..., value)')
    return _Temperature(found, _number_or_field(value, 'fixed_temperature: value'))

fixed_temperature._required = ('region', 'value')


def heat_input(*args, region=None, body=None, watts=None, profile=None):
    ''' A total power (W) put into the part, spread evenly over its surface
        inside `region` (a Shape; any number of regions, one argument after the other, then the power); negative takes heat out.

        region   where the heat goes in: a body, a field or a selected surface (the same as the first argument)
        body     the body the heat goes into, when the regions are bodies that choose WHERE on it: `heat_input(chip, 8, body=part)` heats the part's surface where it meets the chip
        watts    the total power in watts (negative takes heat out)
        profile  a field -- the power is spread in proportion to it (not negative) instead of evenly '''
    found, rest = _regions(args, region, 'heat_input()', body)
    if watts is not None:
        if rest:
            raise TypeError('heat_input(): the power is given once -- as the argument after the regions, or as watts=')
        rest = (watts,)
    watts, = _one(rest, 'heat_input(region, ..., watts)')
    return _HeatInput(found, float(watts), profile=_profile(profile, 'heat_input'))

heat_input._required = ('region', 'watts')


def heat_generation(*args, region=None, watts=None, profile=None):
    ''' A total power (W) generated inside the part, spread evenly through
        its volume inside `region` (a Shape; any number of regions, one argument after the other, then the power) -- e.g. a resistive heater, or
        electronics potted in the part.

        region   where the heat is generated: a body or a field (the same as the first argument)
        watts    the total power in watts
        profile  a field -- spread in proportion to it instead of evenly '''
    found, rest = _regions(args, region, 'heat_generation()')
    if watts is not None:
        if rest:
            raise TypeError('heat_generation(): the power is given once -- as the argument after the regions, or as watts=')
        rest = (watts,)
    watts, = _one(rest, 'heat_generation(region, ..., watts)')
    return _HeatGeneration(found, float(watts), volume=True, profile=_profile(profile, 'heat_generation'))

heat_generation._required = ('region', 'watts')


def convection(*args, region=None, body=None, coefficient=None, ambient=None):
    ''' The part's exposed surface inside `region` (any number of regions, one argument after the other, then the coefficient and the
        ambient temperature) exchanges heat with an ambient temperature.

        region       where the part exchanges heat: a body, a field or a selected surface (the same as the first argument)
        body         the body that exchanges heat, when the regions are bodies that choose WHERE on it: its surface where they are
        coefficient  h in W / (mm^2 K): still air ~5e-6 - 25e-6, forced air ~25e-6 - 250e-6, water ~500e-6 - 1e-2.  A number, or a field --
                     its value at each point of the surface (tetrahedral elements)
        ambient      the ambient temperature (degrees C; 20 when it is not given): a number, or a field '''
    found, rest = _regions(args, region, 'convection()', body)
    if coefficient is not None:
        if len(rest) > 1 or (rest and ambient is not None):
            raise TypeError('convection(): the coefficient is given once -- as the argument after the regions, or as coefficient=')
        rest = (coefficient,) + tuple(rest)
    if len(rest) not in ((1,) if ambient is not None else (1, 2)):
        raise TypeError('convection(region, ..., coefficient, ambient=20): after the regions come the coefficient and, if it is not 20, the ambient temperature (once, as the next argument or as ambient=)')
    coefficient = rest[0]
    if ambient is None:
        ambient = rest[1] if len(rest) == 2 else 20.0
    return _Convection(found, _number_or_field(coefficient, 'convection: coefficient'),
                       _number_or_field(ambient, 'convection: ambient'))

convection._required = ('region', 'coefficient')
convection._positional = ('region', 'coefficient', 'ambient')


_FIELDS = ['temperature', 'heat_flux', 'qx', 'qy', 'qz']
_LABELS = {'temperature': _tr('temperature'), 'heat_flux': _tr('heat flux (W/mm^2)'),
           'qx': _tr('heat flux x (W/mm^2)'), 'qy': _tr('heat flux y (W/mm^2)'), 'qz': _tr('heat flux z (W/mm^2)')}


class _Handle:
    ''' Owns a libfive_thermal object (freed with the last result using it): on the voxel grid, or
        (tet=True) a libfive_tetthermal on a body-fitted tetrahedral mesh '''
    def __init__(self, ptr, tet=False):
        self.ptr = ptr
        self.tet = tet

    def fn(self, name):
        ''' The C function `name` (without libfive_thermal_) for this kind of analysis '''
        return getattr(lib, ('libfive_tetthermal_' if self.tet else 'libfive_thermal_') + name)

    def __del__(self):
        if self.ptr:
            self.fn('delete')(self.ptr)
            self.ptr = None


class ThermalResult(FieldResult):
    ''' The solved thermal analysis.  Fields (Shapes whose value is the
        result at each point, usable in any expression): temperature,
        heat_flux (magnitude, W/mm^2), qx, qy, qz.  Summary: max_temperature,
        min_temperature, max_heat_flux, heat_in, heat_out (W: through the
        fixed temperatures and by convection -- they balance),
        input_temperature (tetrahedral elements: the temperature the heat comes in at, weighted by how much comes in at each place),
        volume (mm^3, of the mesh) and mass (when the material has a density), elements, iterations, seconds. '''

    def __init__(self, handle, shape, material):
        self._handle = handle
        self.shape = shape
        self.material = material
        p = handle.ptr
        self._ranges = {}
        for i, name in enumerate(_FIELDS):
            setattr(self, name, field_shape(handle.fn('field')(p, i)))
            self._ranges[name] = (handle.fn('field_min')(p, i), handle.fn('field_max')(p, i))
        stat = lambda k: handle.fn('stat')(p, k)
        self.elements = int(stat(0))
        self.nodes = int(stat(1))
        self.iterations = int(stat(2))
        self.residual = stat(3)
        self.seconds = stat(4)
        self.heat_in = stat(5)
        self.heat_out_fixed = stat(6)
        self.heat_out_convection = stat(7)
        self.heat_out = self.heat_out_fixed + self.heat_out_convection
        self.min_temperature, self.max_temperature = self._ranges['temperature']
        self.max_heat_flux = self._ranges['heat_flux'][1]
        self.input_temperature = stat(8) if handle.tet else None
        self.volume = stat(11) if handle.tet else None
        density = getattr(material, 'density', None)
        self.mass = float(density) * self.volume if handle.tet and isinstance(density, (int, float)) and density > 0 else None

    def range(self, field='temperature'):
        ''' (min, max) of a field over the part '''
        return self._ranges[field]

    def _mesh_data(self):
        ''' What FielDes draws for "Elements": the mesh the solver used and each tetrahedron's own value of each field -- the temperature at its centre, its own heat flux (constant in it) --
            for an analysis on tetrahedra; None for a voxel grid or when there are more tetrahedra than are drawn (see element_mesh) '''
        cached = getattr(self, '_mesh_full', False)
        if cached is not False:
            return cached
        full = None
        if self._handle.tet and hasattr(lib, 'libfive_tetthermal_element_values'):
            p = self._handle.ptr
            full = _element_mesh(lambda b: lib.libfive_tetthermal_counts(p, b), lambda v, t, f, ft: lib.libfive_tetthermal_mesh(p, v, t, f, ft),
                                 lambda f, buf, n: lib.libfive_tetthermal_element_values(p, f, buf, n), range(len(_FIELDS)), None)
        self._mesh_full = full
        return full

    def _display(self):
        ''' What FielDes shows for the result stated on its own: the part coloured by the temperature
            (the result card switches to the heat flux and its components) '''
        out = colored(self.shape, self.temperature, range=self._ranges['temperature'],
                      label=_LABELS['temperature'], colormap='turbo')
        out._color_fields = [(name, _LABELS[name], getattr(self, name)) + tuple(self._ranges[name])
                             for name in _FIELDS]
        out._color_field_name = 'temperature'
        out._color_detail = float(getattr(self, 'element_size', 0) or 0)
        out._fea_mesh = self._mesh_data()
        if out._fea_mesh is None and self._handle.tet:
            out._fea_elements_note = _elements_note(int(self.elements))       # (more tetrahedra than are drawn: the card says why it has no elements)
        return out

    def __repr__(self):
        return _tr('Thermal analysis: %s elements (%.1f s)\n  temperature %.4g to %.4g \u00b7 max heat flux %.3g W/mm^2 \u00b7 '
                   'heat in %.4g W, out %.4g W') % (
                       '{:,}'.format(self.elements), self.seconds, self.min_temperature, self.max_temperature,
                       self.max_heat_flux, self.heat_in, self.heat_out)


def _conductivity(material, conductivity, what):
    k = conductivity if conductivity is not None else getattr(material, 'conductivity', None)
    if isinstance(k, Shape):
        return k
    if not k or not k > 0:
        raise FeaError('{}: the material has no conductivity -- give conductivity= '
                       '(W / (mm K))'.format(what))
    return k


def _problem(shape, boundary, k, element_size, bounds, what, element='tet'):
    ''' A libfive_thermal problem (in a _Handle) with the boundary
        conditions added; (handle, element size, (lo, hi)) '''
    if not isinstance(shape, Shape):
        raise TypeError('{}: the part must be a Shape'.format(what))
    items = list(boundary if isinstance(boundary, (list, tuple)) else [boundary])
    lo, hi = bounds if bounds is not None else _bounds(shape)
    size = [hi[i] - lo[i] for i in range(3)]
    if element_size is None:
        element_size = default_element_size(shape, lo, hi, 40.0, 1000000) if element == 'tet' else max(size) / 40.0
    element_size = float(element_size)
    pad = 1e-6 * max(max(size), 1e-9)
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i] - pad, hi[i] + pad
    # 'tet' (the default): tetrahedra that follow the part's surface; 'hex' and 'hex_basic' are the
    # same trilinear hexahedron on a voxel grid (there are no locking modes in heat flow)
    if element == 'tet10':
        raise FeaError("{}: the 10-node element (element='tet10') is for static_analysis and modal_analysis; a temperature "
                       "is smooth and linear tetrahedra read it well: use element='tet', the default".format(what))
    if element not in ('tet', 'hex', 'hex_basic'):
        raise ValueError("element is 'tet' (tetrahedra that follow the part, the default) or 'hex' "
                         "(voxel hexahedra)")
    tet = element == 'tet'
    # (fields -- a conductivity, a held temperature, a convection's coefficient and ambient temperature, a profile of a heat
    # input -- work with the tetrahedral elements)
    fields = []
    if isinstance(k, Shape):
        fields.append('the conductivity')
    for b in items:
        if isinstance(b, _Temperature) and isinstance(b.value, Shape):
            fields.append('a fixed temperature')
        elif isinstance(b, _Heat) and b.profile is not None:
            fields.append('a heat profile')
        elif isinstance(b, _Convection) and (isinstance(b.coefficient, Shape) or isinstance(b.ambient, Shape)):
            fields.append('a convection')
    if fields and not tet:
        raise FeaError("{}: {} is a field -- fields work with the tetrahedral elements (element='tet', the default), "
                       "not {!r}".format(what, ', '.join(fields), element))
    if tet:
        if getattr(lib, 'libfive_tetthermal_new', None) is None:
            raise FeaError("this FielDes library is too old for tetrahedral meshing")
        if fields and getattr(lib, 'libfive_tetthermal_set_conductivity_field', None) is None:
            raise FeaError('this FielDes library is too old for fields in a thermal analysis')
        # (a conductivity that is a field is measured against 1: each element has its own value)
        ptr = lib.libfive_tetthermal_new(shape.ptr, region, element_size, 1.0 if isinstance(k, Shape) else float(k))
        if isinstance(k, Shape):
            lib.libfive_tetthermal_set_conductivity_field(ptr, k.ptr)
    else:
        ptr = lib.libfive_thermal_new(shape.ptr, region, element_size, float(k))
    handle = _Handle(ptr, tet)
    if not tet and getattr(lib, 'libfive_thermal_set_element', None) is not None:
        lib.libfive_thermal_set_element(ptr, 1)
    for b in items:
        if isinstance(b, _Temperature):
            if isinstance(b.value, Shape):
                lib.libfive_tetthermal_add_temperature_field(ptr, b.region.ptr, 0.0, b.value.ptr)
            else:
                handle.fn('add_temperature')(ptr, b.region.ptr, b.value)
        elif isinstance(b, _Heat) and b.profile is not None:
            handle.fn('add_generation_profile' if b.volume else 'add_heat_profile')(ptr, b.region.ptr, b.power, b.profile.ptr)
        elif isinstance(b, _Heat) and b.volume:
            handle.fn('add_generation')(ptr, b.region.ptr, b.power)
        elif isinstance(b, _Heat):
            handle.fn('add_heat')(ptr, b.region.ptr, b.power)
        elif isinstance(b, _Convection):
            if isinstance(b.coefficient, Shape) or isinstance(b.ambient, Shape):
                lib.libfive_tetthermal_add_convection_fields(
                    ptr, b.region.ptr,
                    0.0 if isinstance(b.coefficient, Shape) else b.coefficient,
                    b.coefficient.ptr if isinstance(b.coefficient, Shape) else None,
                    0.0 if isinstance(b.ambient, Shape) else b.ambient,
                    b.ambient.ptr if isinstance(b.ambient, Shape) else None)
            else:
                handle.fn('add_convection')(ptr, b.region.ptr, b.coefficient, b.ambient)
        else:
            raise TypeError('boundary items are fixed_temperature(...), heat_input(...), '
                            'heat_generation(...) or convection(...)')
    return handle, element_size, (lo, hi)


_cache = OrderedDict()


@_with_conditions
def thermal_analysis(shape, fixed_temperatures, heat_inputs, heat_generations, convections, material=aluminium, element_size=None,
                     bounds=None, conductivity=None, tolerance=1e-7, element='tet', cache=True):
    ''' Steady-state heat conduction in `shape` with the given boundary
        conditions, one input for each kind (each one item or a list, [] for none):
        fixed_temperatures  fixed_temperature(...) items (at least one of these or of the convections is needed)
        heat_inputs         heat_input(...) items
        heat_generations    heat_generation(...) items
        convections         convection(...) items
        (see the module's description).  Each condition is a model of its own, drawn on `shape`.

        Everything else is as in the analysis below.  '''
    max_iterations = 50000      # (the equation solver's own limit, never reached by a problem that has a solution: not an argument -- it stops by itself when the residual is below `tolerance`)
    boundary = _boundary(fixed_temperatures, heat_inputs, heat_generations, convections, 'thermal_analysis')
    _use_conditions(boundary, shape)
    return _thermal_analysis(shape, boundary, material, element_size, bounds, conductivity, max_iterations, tolerance, element, cache)


def _thermal_analysis(shape, boundary, material=aluminium, element_size=None, bounds=None,
                      conductivity=None, max_iterations=50000, tolerance=1e-7, element='tet',
                      cache=True):
    ''' Steady-state heat conduction in `shape` with the given boundary
        conditions: fixed_temperature(...), heat_input(...),
        heat_generation(...) and convection(...) items (see the module's
        description).

        material: its conductivity is used (W / (mm K)); conductivity=
                  overrides it
        element_size: mm (default: 40 elements along the longest side, or larger when that would make more than a million tetrahedra)
        element:  'tet' (default: tetrahedra that follow the part's surface) or
                  'hex' (voxel hexahedra)

        Returns a ThermalResult: .temperature and .heat_flux fields and
        the heat balance.  An unchanged problem is cached (as a static analysis
        is): running the script again, or a section moving over its fields, does
        not solve it again; a change to the part, the boundary conditions, the
        material or the settings solves the new problem.  cache=False solves
        every time. '''
    k = _conductivity(material, conductivity, 'thermal_analysis')
    # The whole problem by its content, before anything is built (see static_analysis): a problem asked
    # again is neither meshed nor solved again
    ekey = problem_key('thermal', shape=shape, part_bounds=getattr(shape, '_bounds', None),
                       boundary=boundary, material=material, conductivity=conductivity,
                       element_size=element_size, bounds=bounds, max_iterations=max_iterations,
                       tolerance=tolerance, element=element) if cache else None
    if ekey is not None and ekey in _cache:
        _cache.move_to_end(ekey)
        cached = _cache[ekey]
        if cached.shape is shape:
            return cached
        r = object.__new__(ThermalResult)      # the same problem from an identical part
        r.__dict__.update(cached.__dict__)
        r.shape = shape
        return r
    handle, element_size, _ = _problem(shape, boundary, k, element_size, bounds, 'thermal_analysis', element)
    kind = 'tetthermal' if handle.tet else 'thermal'
    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load(kind, ekey, 'thermal analysis')
    if loaded is not None:
        result = ThermalResult(_Handle(loaded[0], handle.tet), shape, material)
        result.element_size = element_size
        _cache[ekey] = result
        return result
    ptr = handle.ptr
    key = None
    if cache and handle.fn('prepare')(ptr):
        # the prepared problem, the settings and the boundary conditions' regions
        # (by their expressions, as the topology optimization's key does)
        items = boundary if isinstance(boundary, (list, tuple)) else [boundary]
        try:
            key = (handle.fn('hash')(ptr), int(max_iterations), float(tolerance), element,
                   tuple((type(b).__name__, shape_key(b.region)) +
                         tuple(sorted((k2, value_key(v)) for k2, v in vars(b).items() if k2 != 'region'))
                         for b in items))
        except Uncacheable:
            key = None
        if key is not None and key in _cache:
            _cache.move_to_end(key)
            cached = _cache[key]
            if cached.shape is shape:
                return cached
            r = object.__new__(ThermalResult)      # the same problem from an identical part
            r.__dict__.update(cached.__dict__)
            r.shape = shape
            return r
    if ekey is not None:
        handle.fn('set_salt')(ptr, result_cache.salt(kind, ekey))
    if not handle.fn('solve')(ptr, int(max_iterations), float(tolerance)):
        raise FeaError(handle.fn('message')(ptr).decode('utf-8', 'replace'))
    result = ThermalResult(handle, shape, material)
    result.element_size = element_size
    result_cache.save(kind, ekey, ptr, 'thermal analysis')
    if key is not None:
        _cache[key] = result
    if ekey is not None:
        _cache[ekey] = result
    while len(_cache) > 16:
        _cache.popitem(last=False)
    return result


################################################################################
# Topology optimization of a thermal analysis: the call is topology_optimization (fea.py), this is its thermal side

class ThermalTopologyResult(TopologyResult):
    """ The result of topology_optimization() for a thermal analysis (see there): the part made best for what is asked of its temperature.
        .density      a field, 0 (no material) .. 1 (solid), in the part
        .shape()      the optimized part, keeping the volume the design has (shape(threshold=0.5): where density > 0.5)
        .analysis     the thermal analysis of the optimized part, made as the one the optimisation was given was: its numbers -- `best.max_temperature`
                      -- are the true ones; .values what was asked, as that part has it; .feasible, .history (the objective per iteration), .table()
        .verify()     the thermal analysis of the optimized part again
        Stated on its own, FielDes shows the optimized part coloured by the density, and the result card steps through the iterations. """

    def keep_threshold(self):
        """ The density above which the part has the volume the design has, counted in the kernel (see TopologyResult.keep_threshold) """
        if getattr(self, '_keep_threshold', None) is not None:
            return self._keep_threshold
        ptr = getattr(getattr(self, '_handle', None), 'ptr', None)
        t = float(lib.libfive_tetthermal_cut_threshold(ptr)) if ptr else 0.5
        self._keep_threshold = t
        return t


_topo_cache = OrderedDict()


def topology_optimization_thermal(analysis, constraints, iterations=None, filter_radius=None, keep=None, avoid=None, grow=False, grow_limit=None,
                                  extrude=None, penalty=3.0, sharpness=16.0, symmetry='auto', tolerance=1e-5, cache=True):
    """ topology_optimization() of a thermal analysis: the layout of material, within the part, that is best for what is asked of its temperature.
        The held temperatures, the heat inputs and generations and the convection of the analysis are the problem (the elements that hold a temperature
        or bring heat in stay solid, and material that nothing joins to a held temperature or a convecting surface is taken out); the conductivity of
        an element is the material's times a power of its density (penalty).  The numbers it can be asked for: max_temperature, min_temperature (the
        peaks of the temperature, as p-norms), max_heat_flux, input_temperature (the heat-weighted mean temperature of the heat inputs: the usual
        goal of a heat sink), temperature(x, y, z), volume, mass and volume_fraction; their sensitivities come from one adjoint solve each """
    from fieldes.stdlib import optimization_measures as measures
    from fieldes.stdlib.fea import _optimization_analyses, _symmetry_request
    if getattr(lib, 'libfive_tetthermal_optimize_density', None) is None:
        raise FeaError('this FielDes library is too old for topology optimization of a thermal analysis')
    iterations = 100 if iterations is None else iterations
    if not constraints:
        raise TypeError("topology_optimization: say what the design is for -- constraints=[minimize('max_temperature'), 'volume_fraction <= 0.3', ...]")
    if grow:
        raise FeaError('topology_optimization: grow=True lets the design of a structure reach out of its part; a thermal design stays in the part (grow=False)')
    analyses = _optimization_analyses(analysis, 'topology_optimization')
    if len(analyses) != 1 or analyses[0].kind != 'thermal':
        raise FeaError('topology_optimization: a thermal analysis is designed on its own: give the one thermal_analysis result (not a list, not together with another kind of analysis)')
    first = analyses[0]
    args = first.arguments
    part = args['shape']
    if not isinstance(part, Shape):
        raise TypeError('topology_optimization: the part must be a Shape')
    if args.get('element', 'tet') != 'tet':
        raise FeaError("topology_optimization: a thermal analysis is designed on the tetrahedral mesh: make it with element='tet' (the default)")
    boundary = _boundary(args['fixed_temperatures'], args['heat_inputs'], args['heat_generations'], args['convections'], 'topology_optimization')
    _use_conditions(boundary, part)
    material, conductivity = args['material'], args.get('conductivity')
    k = _conductivity(material, conductivity, 'topology_optimization')
    element_size, bounds = args.get('element_size'), args.get('bounds')
    mirrors = _symmetry_request(symmetry, 'tet', 'topology_optimization')
    keep = [] if keep is None else list(keep if isinstance(keep, (list, tuple)) else [keep])
    avoid = [] if avoid is None else list(avoid if isinstance(avoid, (list, tuple)) else [avoid])
    keep = keep + keep_regions(part)             # (what the part has excluded stays as it is: its locked fields are regions to keep)
    axes = {None: -1, 'x': 0, 'y': 1, 'z': 2}
    if extrude not in axes:
        raise ValueError("extrude is None, 'x', 'y' or 'z'")
    if isinstance(constraints, str) or callable(constraints):
        constraints = [constraints]
    given = [a.result for a in analyses]

    def baseline_leaf(key):
        return 1.0 if key[0] == 'fraction' else measures.true_value(key, given)

    def baseline(node):
        return measures.evaluate(node, baseline_leaf)

    outputs, goal = measures.outputs_of(constraints, analyses, baseline)
    program = measures.Program(outputs)
    if ('fraction',) not in program.measures:
        program.measures.append(('fraction',))                      # (the share of the part the design uses is always kept: the result reports it)
    start = program.start_fraction()
    ekey = problem_key('thermal_topology', part=part, part_bounds=getattr(part, '_bounds', None), boundary=boundary, material=material,
                       conductivity=conductivity, element_size=element_size, bounds=bounds, program=program.key(), start=start,
                       iterations=int(iterations), filter_radius=filter_radius, keep=keep, avoid=avoid, extrude=extrude, penalty=float(penalty),
                       sharpness=float(sharpness), symmetry=mirrors[2], tolerance=tolerance, algorithm=1) if cache else None
    if ekey is not None and ekey in _topo_cache:
        _topo_cache.move_to_end(ekey)
        cached = _topo_cache[ekey]
        if cached.part is part:
            return cached
        r = object.__new__(ThermalTopologyResult)
        r.__dict__.update(cached.__dict__)
        r.part = part
        return r
    handle, element_size, (lo, hi) = _problem(part, boundary, k, element_size, bounds, 'topology_optimization', 'tet')
    settings = {'volume_fraction': start, 'penalty': float(penalty), 'filter_radius': float(filter_radius or 0.0), 'iterations': int(iterations),
                'extrude': axes[extrude], 'element': 'tet', 'sharpness': float(sharpness), 'symmetry': mirrors[2], 'grow': 0, 'grow_limit': 0.0}

    def finish(handle, ptr, seconds, symmetric=None, saved=None):
        """ The result from a solved problem (just optimised, or read back from its file: `symmetric`, what the file says; `saved`, its numbers) """
        density = field_shape(lib.libfive_tetthermal_density(ptr))
        buf = (ctypes.c_double * 1000)()
        m = lib.libfive_tetthermal_history(ptr, buf, 1000)
        history = [buf[i] for i in range(min(m, 1000))]
        densities = []
        for j in range(len(history)):
            f = lib.libfive_tetthermal_density_at(ptr, j)
            if not f:
                break
            densities.append(field_shape(f))
        if saved is not None:
            bases, outs = saved['bases'], saved['outputs']
            history = saved.get('history') or history
            share = float(saved['share'])
        else:
            bases, outs = [], []
            for j in range(len(program.measures)):
                n = lib.libfive_tetthermal_measure_history(ptr, 0, j, buf, 1000)
                bases.append([buf[i] for i in range(n)])
            for j in range(len(outputs)):
                n = lib.libfive_tetthermal_measure_history(ptr, 1, j, buf, 1000)
                outs.append([buf[i] for i in range(n)])
            share = bases[program.measures.index(('fraction',))][-1] if history else start
        stats = {'volume_fraction': share, 'iterations': len(history), 'seconds': seconds}
        result = ThermalTopologyResult(handle, part, density, history, settings, element_size, (list(lo), list(hi)), [], [], material, stats)
        result.densities = densities
        result._set_problem(analyses, outputs, program, bases, outs, goal)
        # The planes the design was kept symmetric about: {'x': [position, how]} -- asked for (1), or found (2: and then said)
        if symmetric is None:
            symmetric = {}
            for j, name in enumerate('xyz'):
                at = ctypes.c_double()
                how = lib.libfive_tetthermal_mirror(ptr, j, ctypes.byref(at))
                if how:
                    symmetric[name] = [float(at.value), int(how)]
        found = ['%s = %.4g' % (n, v[0]) for n, v in symmetric.items() if v[1] == 2]
        if found:
            print('topology_optimization: the part and its conditions are symmetric about %s: the design is kept symmetric '
                  '(symmetry=None leaves it alone).' % ' and '.join(found))
        result.symmetry = {n: v[0] for n, v in symmetric.items()}
        result._symmetry_info = symmetric
        # A part that falls into pieces cannot do what a part is for: say so (a link thinner than a millimetre of the cut surface is not one)
        width = 2.0 * (settings['filter_radius'] or 1.5 * element_size)
        result.pieces = result._join(lib.libfive_tetthermal_pieces, ptr, 1.0 / width)
        if result.pieces > 1:
            print('topology_optimization: the optimised part is in %d separate pieces (nothing joins them). Try more material (a looser limit on the volume) -- '
                  'the loose scraps show what the part would become -- or fewer or smaller keep regions.' % result.pieces)
        result._verify_into()
        if any(isinstance(b, _HeatGeneration) for b in boundary):
            print('topology_optimization: a heat generation is a condition of the design space: its heat is made where its region is, material or not (the classic volume-to-point problem), '
                  'while the analysis of the optimized part makes it in the part only -- so the temperatures of that analysis are lower than the optimiser had.')
        return result

    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load('tetthermal', ekey, 'thermal topology optimization')
    if loaded is not None and loaded[1].get('measures'):
        result = finish(_Handle(loaded[0], True), loaded[0], float(loaded[1].get('seconds', 0)), dict(loaded[1].get('symmetry') or {}), loaded[1]['measures'])
        _topo_cache[ekey] = result
        return result
    ptr = handle.ptr
    if not lib.libfive_tetthermal_prepare(ptr):
        raise FeaError('topology_optimization: ' + lib.libfive_tetthermal_message(ptr).decode('utf-8', 'replace'))
    keep_arr = (ctypes.c_void_p * max(1, len(keep)))(*[_shape(r, 'keep').ptr for r in keep])
    avoid_arr = (ctypes.c_void_p * max(1, len(avoid)))(*[_shape(r, 'avoid').ptr for r in avoid])
    if mirrors[0]:
        lib.libfive_tetthermal_set_symmetry_auto(ptr, 1)
    for axis, at in mirrors[1]:
        lib.libfive_tetthermal_add_mirror(ptr, axis, 0.0 if at is None else float(at), 1 if at is None else 0)
    if ekey is not None:
        lib.libfive_tetthermal_set_salt(ptr, result_cache.salt('tetthermal', ekey))
    lib.libfive_tetthermal_opt_clear(ptr)
    for key in program.measures:
        lib.libfive_tetthermal_opt_measure(ptr, *program.thermal_measure(key))
    for code, value, index in program.ops:
        lib.libfive_tetthermal_opt_op(ptr, int(code), float(value), int(index))
    lib.libfive_tetthermal_opt_start(ptr, float(start), (outputs[0].text or 'constraints').encode('utf-8'), -1.0 if goal == 'maximize' else 1.0, float(sharpness))
    t0 = time.time()
    ok = lib.libfive_tetthermal_optimize_density(ptr, settings['penalty'], settings['filter_radius'], settings['iterations'], keep_arr, len(keep), avoid_arr,
                                                 len(avoid), 50000, float(tolerance), settings['extrude'])
    if not ok:
        raise FeaError('topology_optimization: ' + lib.libfive_tetthermal_message(ptr).decode('utf-8', 'replace'))
    result = finish(handle, ptr, time.time() - t0)
    meta = {'seconds': result.seconds, 'symmetry': result._symmetry_info,
            'measures': {'bases': result._bases, 'outputs': result._outs, 'share': result.volume_fraction, 'history': list(result.history)}}
    result_cache.save('tetthermal', ekey, ptr, 'thermal topology optimization', meta)
    if ekey is not None:
        _topo_cache[ekey] = result
        while len(_topo_cache) > 8:
            _topo_cache.popitem(last=False)
    return result


ThermalResult._as_field = lambda self: self.temperature
ThermalResult.fields = property(lambda self: OrderedDict((name, getattr(self, name)) for name in _FIELDS))
