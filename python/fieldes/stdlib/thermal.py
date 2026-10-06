'''
Steady-state thermal analysis (heat conduction) of FielDes shapes.

    from fieldes import *

    part = ...                                          # any Shape, in mm
    result = thermal_analysis(part, [
        fixed_temperature(base_region, 20),             # held at 20 degrees C
        heat_input(chip_region, 5.0),                   # 5 W into the part here
        convection(fins_region, 25e-6, ambient=20)],    # air cooling
        material=aluminium, element_size=1.0)
    result                               # the part coloured by temperature (FielDes)
    thicker = part - 0.2 * result.heat_flux   # results are fields like any other

    # the material layout (30 % of the part) that keeps the heat input coolest
    design = thermal_topology_optimization(part, [...], volume_fraction=0.3)
    design.shape()

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
filled at the surface), and a preconditioned conjugate-gradient solve.  Thermal
topology optimization designs on the voxel grid only.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import time
from collections import OrderedDict

from fieldes.ffi import lib, libfive_region_t
from fieldes.shape import Shape
from fieldes.stdlib.excluded import keep_regions
from fieldes.stdlib.fea import FeaError, TopologyResult, aluminium, colored, _bounds, _shape
from fieldes.stdlib.content_cache import Uncacheable, problem_key, shape_key, value_key
from fieldes.stdlib import result_cache

__all__ = ['fixed_temperature', 'heat_input', 'heat_generation', 'convection',
           'thermal_analysis', 'ThermalResult', 'thermal_topology_optimization',
           'ThermalTopologyResult']


class _Temperature:
    def __init__(self, region, value):
        self.region, self.value = region, value


class _Heat:
    def __init__(self, region, power, volume=False, profile=None):
        self.region, self.power, self.volume, self.profile = region, power, volume, profile


class _Convection:
    def __init__(self, region, coefficient, ambient):
        self.region, self.coefficient, self.ambient = region, coefficient, ambient


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


def fixed_temperature(region, value):
    ''' The part held at temperature `value` wherever it lies inside
        `region` (a Shape).  `value` can be a field: the temperature at each point (tetrahedral elements) '''
    return _Temperature(_shape(region, 'fixed_temperature(region)'), _number_or_field(value, 'fixed_temperature: value'))


def heat_input(region, watts, profile=None):
    ''' A total power (W) put into the part, spread evenly over its surface
        inside `region` (a Shape); negative takes heat out.  profile: a field -- the power is spread in
        proportion to it (not negative) instead of evenly '''
    return _Heat(_shape(region, 'heat_input(region)'), float(watts), profile=_profile(profile, 'heat_input'))


def heat_generation(region, watts, profile=None):
    ''' A total power (W) generated inside the part, spread evenly through
        its volume inside `region` (a Shape) -- e.g. a resistive heater, or
        electronics potted in the part.  profile: a field -- spread in proportion to it instead of evenly '''
    return _Heat(_shape(region, 'heat_generation(region)'), float(watts), volume=True,
                 profile=_profile(profile, 'heat_generation'))


def convection(region, coefficient, ambient=20.0):
    ''' The part's exposed surface inside `region` exchanges heat with an
        ambient temperature: coefficient h in W / (mm^2 K) (still air
        ~5e-6 - 25e-6, forced air ~25e-6 - 250e-6, water ~500e-6 - 1e-2).  The coefficient and the ambient
        temperature can each be a field: their values at each point of the surface (tetrahedral elements) '''
    return _Convection(_shape(region, 'convection(region)'), _number_or_field(coefficient, 'convection: coefficient'),
                       _number_or_field(ambient, 'convection: ambient'))


_FIELDS = ['temperature', 'heat_flux', 'qx', 'qy', 'qz']
_LABELS = {'temperature': 'temperature', 'heat_flux': 'heat flux (W/mm^2)',
           'qx': 'heat flux x (W/mm^2)', 'qy': 'heat flux y (W/mm^2)', 'qz': 'heat flux z (W/mm^2)'}


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


class ThermalResult:
    ''' The solved thermal analysis.  Fields (Shapes whose value is the
        result at each point, usable in any expression): temperature,
        heat_flux (magnitude, W/mm^2), qx, qy, qz.  Summary: max_temperature,
        min_temperature, max_heat_flux, heat_in, heat_out (W: through the
        fixed temperatures and by convection -- they balance), elements,
        iterations, seconds. '''

    def __init__(self, handle, shape, material):
        self._handle = handle
        self.shape = shape
        self.material = material
        p = handle.ptr
        self._ranges = {}
        for i, name in enumerate(_FIELDS):
            setattr(self, name, Shape(handle.fn('field')(p, i)))
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

    def range(self, field='temperature'):
        ''' (min, max) of a field over the part '''
        return self._ranges[field]

    def _display(self):
        ''' What FielDes shows for the result stated on its own: the part coloured by the temperature
            (the result card switches to the heat flux and its components) '''
        out = colored(self.shape, self.temperature, range=self._ranges['temperature'],
                      label=_LABELS['temperature'], colormap='turbo')
        out._color_fields = [(name, _LABELS[name], getattr(self, name)) + tuple(self._ranges[name])
                             for name in _FIELDS]
        out._color_field_name = 'temperature'
        out._color_detail = float(getattr(self, 'element_size', 0) or 0)
        return out

    def __repr__(self):
        return ('Thermal analysis: {:,} elements ({:.1f} s)\n'
                '  temperature {:.4g} to {:.4g} \u00b7 max heat flux {:.3g} W/mm^2 \u00b7 '
                'heat in {:.4g} W, out {:.4g} W').format(
                    self.elements, self.seconds, self.min_temperature, self.max_temperature,
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
        element_size = max(size) / 40.0
    element_size = float(element_size)
    pad = 1e-6 * max(max(size), 1e-9)
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i] - pad, hi[i] + pad
    # 'tet' (the default): tetrahedra that follow the part's surface; 'hex' and 'hex_basic' are the
    # same trilinear hexahedron on a voxel grid (there are no locking modes in heat flow)
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


def thermal_analysis(shape, boundary, material=aluminium, element_size=None, bounds=None,
                     conductivity=None, max_iterations=50000, tolerance=1e-7, element='tet',
                     cache=True):
    ''' Steady-state heat conduction in `shape` with the given boundary
        conditions: fixed_temperature(...), heat_input(...),
        heat_generation(...) and convection(...) items (see the module's
        description).

        material: its conductivity is used (W / (mm K)); conductivity=
                  overrides it
        element_size: mm (default: 40 elements along the longest side)
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
# Thermal topology optimization

class ThermalTopologyResult(TopologyResult):
    ''' The result of thermal_topology_optimization():
        .density      a field, 0 (no material) .. 1 (solid), in the part
        .shape()      the optimized part, keeping the volume fraction asked
                      for (shape(threshold=0.5): where density > 0.5)
        .temperature  the heat-weighted mean temperature of the heat inputs
                      at each iteration -- lower is better
        .volume_fraction, .iterations, .seconds
        .verify()     a thermal analysis of the optimized part '''

    def __init__(self, handle, part, density, history, settings, element_size, bounds,
                 boundary, material, conductivity, stats):
        TopologyResult.__init__(self, handle, part, density, history, settings, element_size,
                                bounds, [], [], material, stats)
        self.temperature = history
        self._boundary = boundary
        self._conductivity = conductivity

    def verify(self, threshold=None, element_size=None):
        ''' A thermal analysis of the optimized part with the same boundary
            conditions and material '''
        return thermal_analysis(self.shape(threshold), self._boundary, self.material,
                                element_size=element_size or self.element_size,
                                bounds=self.bounds, conductivity=self._conductivity,
                                element=self.settings.get('element', 'hex'))

    def __repr__(self):
        t = self.temperature
        return ('Thermal topology optimization: {:.0f} % of the part kept, heat input temperature '
                '{:.4g} -> {:.4g} ({} iterations, {:.1f} s)').format(
                    100 * self.volume_fraction, t[0] if t else 0, t[-1] if t else 0,
                    self.iterations, self.seconds)


_topo_cache = OrderedDict()


def thermal_topology_optimization(part, boundary, material=aluminium, volume_fraction=0.3,
                                  element_size=None, iterations=60, filter_radius=None,
                                  keep=None, avoid=None, extrude=None, penalty=3.0, move=0.2,
                                  bounds=None, conductivity=None, max_iterations=20000,
                                  tolerance=1e-6, cache=True, element='hex'):
    ''' Thermal topology optimization: the material layout, using
        `volume_fraction` of `part` (the design space), that keeps the heat
        coolest where it goes in -- the heat-weighted mean temperature of
        the heat inputs is minimised -- for the given boundary conditions
        (as in thermal_analysis).  The rest of the design space is taken as
        empty (a thousandth of the material's conductivity).

        Heat inputs (heat_input, heat_generation) are needed; the
        fixed_temperature regions are the heat sinks.  Convection follows
        the design: it cools the optimized part's surface wherever it lies
        inside a convection region -- give the region as the air around
        and inside the design space, and fins grown into it are cooled.
        The classic "volume-to-point" problem -- heat generated all through
        a part, one small sink -- grows branching conductor trees.

        extrude: 'x', 'y' or 'z' -- the design is constant along that axis
                (outside the keep regions), every hole a channel right
                through: an extruded or pin-fin heat sink.  For air cooling
                use it: with one heat transfer coefficient everywhere, a
                closed pocket deep in the part would count as cooled like an
                open face.

        keep:   regions (Shapes, or a list) that must stay solid; the
                material at fixed temperatures and heat inputs always stays
        avoid:  regions that must stay empty
        element_size:  mm (default: 40 elements along the longest side)
        filter_radius: the smallest member size scale, mm (default 1.5
                elements)

        Returns a ThermalTopologyResult: .density (a field), .shape() (the
        optimized part), .temperature (per iteration), .verify().  An
        unchanged problem is cached, so re-running a script is instant. '''
    if element == 'tet':
        raise FeaError("thermal topology optimization designs one density per cell of a regular grid: "
                       "there is no tetrahedral mesh option (element='hex')")
    k = _conductivity(material, conductivity, 'thermal_topology_optimization')
    keep = [] if keep is None else list(keep if isinstance(keep, (list, tuple)) else [keep])
    avoid = [] if avoid is None else list(avoid if isinstance(avoid, (list, tuple)) else [avoid])
    # (what the part has excluded stays as it is: its locked fields are regions to keep)
    keep = keep + keep_regions(part)
    # The whole problem by its content, before anything is built (see static_analysis)
    ekey = problem_key('thermal_topology', part=part, part_bounds=getattr(part, '_bounds', None),
                       boundary=boundary, material=material, conductivity=conductivity,
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
        r = object.__new__(ThermalTopologyResult)
        r.__dict__.update(cached.__dict__)
        r.part = part
        return r
    handle, element_size, (lo, hi) = _problem(part, boundary, k, element_size, bounds,
                                              'thermal_topology_optimization', element)
    axes = {None: -1, 'x': 0, 'y': 1, 'z': 2}
    if extrude not in axes:
        raise ValueError("extrude is None, 'x', 'y' or 'z'")
    settings = {'volume_fraction': float(volume_fraction), 'penalty': float(penalty),
                'filter_radius': float(filter_radius or 0.0), 'iterations': int(iterations),
                'move': float(move), 'extrude': axes[extrude], 'element': element}
    blist = list(boundary if isinstance(boundary, (list, tuple)) else [boundary])
    kind = 'tetthermal' if handle.tet else 'thermal'

    def finish(handle, ptr, seconds):
        ''' The result from a solved problem (just optimised, or read back from its file) '''
        density = Shape(lib.libfive_thermal_density(ptr))
        hist = (ctypes.c_double * 1000)()
        m = lib.libfive_thermal_history(ptr, hist, 1000)
        history = [hist[i] for i in range(min(m, 1000))]
        stats = {'volume_fraction': settings['volume_fraction'], 'iterations': len(history), 'seconds': seconds}
        return ThermalTopologyResult(handle, part, density, history, settings, element_size,
                                     (lo, hi), blist, material, conductivity, stats)

    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load(kind, ekey, 'thermal topology optimization')
    if loaded is not None:
        result = finish(_Handle(loaded[0], handle.tet), loaded[0], float(loaded[1].get('seconds', 0)))
        _topo_cache[ekey] = result
        return result
    ptr = handle.ptr
    if not lib.libfive_thermal_prepare(ptr):
        raise FeaError(lib.libfive_thermal_message(ptr).decode('utf-8', 'replace'))
    # cache key: the prepared problem, the settings, the keep / avoid regions
    # and the boundary conditions' regions (by their expressions: convection
    # reaches inside the design space, where the prepared problem doesn't
    # record it)
    items = boundary if isinstance(boundary, (list, tuple)) else [boundary]
    try:
        key = (lib.libfive_thermal_hash(ptr), tuple(sorted(settings.items())),
               tuple(shape_key(_shape(r, 'keep')) for r in keep),
               tuple(shape_key(_shape(r, 'avoid')) for r in avoid),
               tuple((type(b).__name__, shape_key(b.region)) +
                     tuple(sorted((k, value_key(v)) for k, v in vars(b).items() if k != 'region'))
                     for b in items))
    except Uncacheable:
        key = None
    if cache and key is not None and key in _topo_cache:
        cached = _topo_cache[key]
        _topo_cache.move_to_end(key)
        if cached.part is part:
            return cached
        r = object.__new__(ThermalTopologyResult)
        r.__dict__.update(cached.__dict__)
        r.part = part
        return r

    keep_arr = (ctypes.c_void_p * max(1, len(keep)))(*[r.ptr for r in keep])
    avoid_arr = (ctypes.c_void_p * max(1, len(avoid)))(*[r.ptr for r in avoid])
    if ekey is not None:
        lib.libfive_thermal_set_salt(ptr, result_cache.salt(kind, ekey))
    t0 = time.time()
    ok = lib.libfive_thermal_optimize(ptr, settings['volume_fraction'], settings['penalty'],
                                      settings['filter_radius'], settings['iterations'],
                                      settings['move'], keep_arr, len(keep), avoid_arr,
                                      len(avoid), int(max_iterations), float(tolerance),
                                      settings['extrude'])
    if not ok:
        raise FeaError(lib.libfive_thermal_message(ptr).decode('utf-8', 'replace'))
    result = finish(handle, ptr, time.time() - t0)
    result_cache.save(kind, ekey, ptr, 'thermal topology optimization', {'seconds': result.seconds})
    if cache and key is not None:
        _topo_cache[key] = result
    if ekey is not None:
        _topo_cache[ekey] = result
    while len(_topo_cache) > 8:
        _topo_cache.popitem(last=False)
    return result
