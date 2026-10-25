'''
Fluid flow analysis of FielDes shapes: incompressible laminar flow, steady or in time, around a body.

    from fieldes import *

    duct = box((0, 0, 0), (80, 40, 4))             # the DOMAIN: the fluid, and the place the body is in
    post = cylinder(5, 6, (24, 20, -1))                # the BODY the flow goes around
    result = fluid_analysis(
        post,
        duct,
        inlets=[inlet(box((-1, -1, -1), (0.01, 41, 5)), speed=4)],                             # mm/s, in
        outlets=[outlet(box((79.99, -1, -1), (81, 41, 5)), pressure=0)],                       # MPa
        boundaries=[],                                                                               # (none: the rest of the surface is wall)
        fluid=water, element_size=1.0)
    result                                               # the flow (FielDes: the fluid coloured by the speed,
                                                         # streamlines with moving particles; the result card)
    print(result.wall_force[0], 'N')                     # the force on the walls, the body's: its drag
    thicker = part + 0.02 * result.pressure              # results are fields like any other

It takes the body, the domain, the three kinds of boundary conditions and the fluid -- what topology_optimization designs the body for -- and
solves the flow once, around the body as it is (the way static_analysis is to topology_optimization).  The domain is a shape, the fluid
where its field is negative, with the body's place in it: the fluid is the domain with the body cut out (difference(domain, body)).
The boundary conditions are regions (shapes), on the surface of the fluid.  Each kind is an input of its own -- inlets, outlets, boundaries (walls and slips) -- that is needed (the call is written
with a placeholder for each) and takes one item or a list ([] says there is none); each condition is a model of its own, with an eye, drawn on
the fluid domain:
  inlet(region, velocity= | speed= | flow_rate=, profile='uniform' | 'developed')
                            the fluid comes in: a velocity vector (mm/s), or a mean speed along the
                            inward normal, or a flow rate (mm^3/s).  The speed is the MEAN over the
                            inlet, matched exactly on the mesh.  'developed': the fully developed
                            profile of that cross-section (parabolic in a pipe); 'uniform': a plug
                            with the no-slip rim
  outlet(region, pressure)  the fluid leaves at that pressure (MPa); the "do-nothing" condition,
                            so put an outlet where the flow leaves parallel to the walls
  wall(region, velocity)    a moving wall (no-slip at that velocity); every surface that is in no
                            region is a wall at rest
  slip(region)              a symmetry plane or a frictionless wall: no flow through it
The fluid: Fluid(name, density, viscosity) in t/mm^3 and MPa s (= N s / mm^2); water, air and oil are
predefined.  Gravity (mm/s^2) is a body force.

The equations are the steady Navier-Stokes equations (rho (u.grad) u - mu laplace u + grad p = rho g,
div u = 0) on the same tetrahedra that follow the fluid's surface as the structural analyses use,
linear in the velocity and the pressure and stabilised (SUPG / PSPG); the nonlinearity is solved by
Picard then Newton iterations from the Stokes solution; stokes=True leaves the convection out
(creeping flow: Reynolds numbers well below 1).  The results are fields: speed, vx, vy, vz,
pressure, total_pressure, shear_rate, vorticity; and numbers: the flows, the pressure drop, the
force on the walls, the dissipation, the Reynolds number.

Limits, plainly: laminar and steady only -- no turbulence model, nothing time-dependent; a flow
whose Reynolds number is beyond the laminar range (about 2000 in a pipe) is not described by this;
no boundary-layer (inflation) elements: the mesh must be fine enough across the passages (the
result says how many elements lie across); the pressure is linear in each element, so its peak at
a corner is smeared over an element; no free surfaces, no heat transfer with the flow (yet).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import types
import ctypes
import math
import time
from collections import OrderedDict

from fieldes.ffi import lib, libfive_region_t
from fieldes.i18n import tr as _tr
from fieldes.shape import Shape, field_shape
from fieldes.stdlib.excluded import keep_regions, carry_locks
from fieldes.stdlib.fea import (FeaError, FieldResult, TopologyResult, colored, _bounds, _shape, _regions, _Condition, _register_slots, _given,
                                _use_conditions, _with_conditions, _symmetry_request)
from fieldes.stdlib.csg import difference as _difference
from fieldes.stdlib.content_cache import Uncacheable, problem_key, shape_key
from fieldes.stdlib import result_cache
from fieldes.stdlib.element_mesh import element_mesh as _element_mesh, elements_note as _elements_note

__all__ = ['Fluid', 'water', 'air', 'oil', 'glycerol', 'inlet', 'outlet', 'wall', 'slip', 'symmetry',
           'fluid_analysis', 'FluidResult', 'FluidStep', 'FlowTopologyResult']


def _scalar(value, what):
    ''' A number of the flow problem.  (The flow solver takes numbers: its fluid, its inlets, outlets and walls are not fields.
        Fields work in the other analyses -- see Material, force, fixed_temperature, convection.) '''
    if isinstance(value, Shape):
        raise TypeError('{} is a number: the flow solver takes numbers, not fields'.format(what))
    return float(value)


class Fluid:
    ''' A Newtonian fluid: density (t/mm^3) and dynamic viscosity (MPa s = N s / mm^2).
        Water: 1.0e-9 t/mm^3, 1.0e-9 MPa s (kinematic viscosity 1 mm^2/s). '''
    def __init__(self, name, density, viscosity):
        self.name = name
        self.density = _scalar(density, 'Fluid: the density')
        self.viscosity = _scalar(viscosity, 'Fluid: the viscosity')
        if not self.density > 0 or not self.viscosity > 0:
            raise ValueError('Fluid: the density and the viscosity must be positive')

    @property
    def kinematic_viscosity(self):
        ''' mu / rho, mm^2/s '''
        return self.viscosity / self.density

    def __repr__(self):
        return 'Fluid({!r}, density={:g} t/mm^3, viscosity={:g} MPa s)'.format(self.name, self.density, self.viscosity)


# (at about 20 C: 1 kg/m^3 = 1e-12 t/mm^3, 1 Pa s = 1e-6 MPa s)
water = Fluid('water', 1.0e-9, 1.0e-9)
air = Fluid('air', 1.2e-12, 1.8e-11)
oil = Fluid('oil (SAE 30)', 0.87e-9, 0.2e-6)
glycerol = Fluid('glycerol', 1.26e-9, 1.4e-6)


class _Inlet(_Condition):
    _call = 'inlet(...)'

    def __init__(self, region, direction, speed, flow_rate, profile):
        self.region, self.direction, self.speed, self.flow_rate, self.profile = region, direction, speed, flow_rate, profile


class _Outlet(_Condition):
    _call = 'outlet(...)'

    def __init__(self, region, pressure):
        self.region, self.pressure = region, pressure


class _Wall(_Condition):
    _call = 'wall(...)'

    def __init__(self, region, velocity):
        self.region, self.velocity = region, velocity


class _Slip(_Condition):
    _call = 'slip(...)'

    def __init__(self, region):
        self.region = region


_register_slots({'inlets': ((_Inlet,), 'inlet(...) items'), 'outlets': ((_Outlet,), 'outlet(...) items'),
                 'boundaries': ((_Wall, _Slip), 'wall(...) and slip(...) items')})


def _flow_conditions(inlets, outlets, boundaries, what):
    ''' The conditions of a flow, one list: what each input is given '''
    return _given(inlets, 'inlets', what) + _given(outlets, 'outlets', what) + _given(boundaries, 'boundaries', what)


_PROFILES = {'uniform': 0, 'developed': 1}


def inlet(*regions, region=None, body=None, velocity=None, speed=None, flow_rate=None, profile='uniform'):
    ''' The fluid comes in through the surface inside `region` (a Shape; any number of regions, one argument after the other).  `body=` is the body the surface is of (the domain),
        when the regions are bodies that choose WHERE on it: its surface where they are.  One of:
        velocity   a vector (mm/s): the direction, and the mean speed over the inlet
        speed      a mean speed (mm/s) along the inlet's inward normal
        flow_rate  a flow rate (mm^3/s) along the inward normal
        profile    'uniform' (a plug, zero on the no-slip rim) or 'developed' (the fully developed
                   profile of the inlet's cross-section: parabolic in a round pipe)
        The speed or flow rate is matched exactly on the mesh (the flux through the inlet's triangles). '''
    region, rest = _regions(regions, region, 'inlet()', body)
    if rest:
        if len(rest) == 1 and velocity is None:
            velocity = rest[0]
        else:
            raise TypeError('inlet(region, ..., velocity=, speed= or flow_rate=): only the velocity can follow the regions')
    if profile not in _PROFILES:
        raise ValueError("inlet: profile is 'uniform' or 'developed'")
    given = sum(x is not None for x in (velocity, speed, flow_rate))
    if given != 1:
        raise ValueError('inlet: give one of velocity=, speed= or flow_rate=')
    direction = (0.0, 0.0, 0.0)
    if velocity is not None:
        direction = tuple(_scalar(v, 'inlet: velocity') for v in velocity)
        if len(direction) != 3 or not sum(v * v for v in direction) > 0:
            raise ValueError('inlet: velocity must be a non-zero (vx, vy, vz)')
    s = _scalar(speed, 'inlet: speed') if speed is not None else 0.0
    q = _scalar(flow_rate, 'inlet: flow_rate') if flow_rate is not None else 0.0
    if speed is not None and not s > 0:
        raise ValueError('inlet: the speed must be positive (it is into the fluid)')
    if flow_rate is not None and not q > 0:
        raise ValueError('inlet: the flow rate must be positive (it is into the fluid)')
    return _Inlet(region, direction, s, q, profile)

inlet._required = ('region', 'speed')


def outlet(*regions, region=None, body=None, pressure=0.0):
    ''' The fluid leaves through the surface inside `region` (any number of regions, one argument after the other) at `pressure` (MPa, 0 by default; the
        pressure field is relative to it).  Put it where the flow leaves parallel to the walls.  `body=` is the body the surface is of, when the regions are bodies that choose
        WHERE on it. '''
    found, rest = _regions(regions, region, 'outlet()', body)
    if len(rest) > 1:
        raise TypeError('outlet(region, ..., pressure=): only the pressure can follow the regions')
    if rest:
        pressure = rest[0]
    return _Outlet(found, _scalar(pressure, 'outlet: pressure'))

outlet._required = ('region',)


def wall(*regions, region=None, body=None, velocity=(0.0, 0.0, 0.0)):
    ''' A wall moving with `velocity` (mm/s; no-slip) inside `region` (any number of regions, one argument after the other).  Surfaces in no region are walls at rest, so
        this is for moving walls, and for naming a wall whose force is wanted (wall_forces).  `body=` is the body the wall is of, when the regions are bodies that choose
        WHERE on it. '''
    found, rest = _regions(regions, region, 'wall()', body)
    if len(rest) > 1:
        raise TypeError('wall(region, ..., velocity=): only the velocity can follow the regions')
    if rest:
        velocity = rest[0]
    v = tuple(_scalar(x, 'wall: velocity') for x in velocity)
    if len(v) != 3:
        raise ValueError('wall: velocity must be (vx, vy, vz)')
    return _Wall(found, v)

wall._required = ('region',)


def slip(*regions, region=None, body=None):
    ''' A symmetry plane or frictionless wall inside `region` (any number of regions, one argument after the other): nothing flows through it,
        the fluid slides along it.  `body=` is the body the wall is of, when the regions are bodies that choose WHERE on it. '''
    found, rest = _regions(regions, region, 'slip()', body)
    if rest:
        raise TypeError('slip(region, ...): only regions can be given')
    return _Slip(found)

slip._required = ('region',)


symmetry = slip

_FIELDS = ['speed', 'vx', 'vy', 'vz', 'pressure', 'total_pressure', 'shear_rate', 'vorticity']
_LABELS = {'speed': _tr('speed (mm/s)'), 'vx': _tr('velocity x (mm/s)'), 'vy': _tr('velocity y (mm/s)'), 'vz': _tr('velocity z (mm/s)'),
           'pressure': _tr('pressure (MPa)'), 'total_pressure': _tr('total pressure (MPa)'), 'shear_rate': _tr('shear rate (1/s)'),
           'vorticity': _tr('vorticity (1/s)')}


class _Handle:
    ''' Owns a libfive_tetflow object (freed with the last result using it) '''
    def __init__(self, ptr):
        self.ptr = ptr

    def __del__(self):
        if self.ptr:
            lib.libfive_tetflow_delete(self.ptr)
            self.ptr = None


class FluidResult(FieldResult):
    ''' The solved flow.  Fields (Shapes whose value is the result at each point, usable in any
        expression): speed, vx, vy, vz (mm/s), pressure, total_pressure (MPa), shear_rate,
        vorticity (1/s).  Numbers: inlet_flow, outlet_flow (mm^3/s; inlet_flows / outlet_flows per
        item), wall_flow (net flow in through the walls: a moving wall, the rounded rim of an inlet),
        mass_imbalance (|in + wall_flow - out| / in), pressure_drop (MPa, mean inlet minus mean outlet), max_speed,
        wall_force (N, on all the walls; wall_forces per wall(...) item), dissipation (W),
        reynolds (rho U D_h / mu of the inlet), cell_reynolds, hydraulic_diameter,
        elements_across (how many elements lie across the passages), elements, nodes, iterations,
        residual, converged, seconds, warning.  .steps (a FluidStep each): the flow after every solver
        iteration of a steady solve (the Stokes start first, the converged flow last), or every stored
        time of a flow in time (fluid_analysis(..., time=); .times); the result's own fields are the
        last step's.  streamlines() gives the paths of particles through the flow.
        Stated on its own, FielDes shows the fluid coloured by the speed, as the other analyses are shown,
        with streamlines from the inlets and particles moving along them drawn over it; the result card
        switches the field and steps through the iterations or the times (slider, play / pause). '''

    def __init__(self, handle, shape, fluid, items):
        self._handle = handle
        self.shape = shape
        self.fluid = fluid
        p = handle.ptr
        self._ranges = {}
        for i, name in enumerate(_FIELDS):
            setattr(self, name, field_shape(lib.libfive_tetflow_field(p, i)))
            self._ranges[name] = (lib.libfive_tetflow_field_min(p, i), lib.libfive_tetflow_field_max(p, i))
        stat = lambda k: lib.libfive_tetflow_stat(p, k)
        self.elements = int(stat(0))
        self.nodes = int(stat(1))
        self.unknowns = int(stat(2))
        self.iterations = int(stat(3))
        self.residual = stat(4)
        self.seconds = stat(5)
        self.inlet_flow = stat(6)
        self.outlet_flow = stat(7)
        self.mass_imbalance = stat(8)
        self.pressure_drop = stat(9)
        self.max_speed = stat(10)
        self.dissipation = stat(11) * 1e-3           # N mm / s -> W
        self.reynolds = stat(12)
        self.cell_reynolds = stat(13)
        self.hydraulic_diameter = stat(14)
        self.elements_across = stat(15)
        self.wall_force = (stat(16), stat(17), stat(18))
        self.converged = bool(stat(19))
        self.linear_solver = ('direct', 'iterative', 'iterative, then direct')[int(stat(20))]
        self.wall_flow = stat(21)
        buf = (ctypes.c_double * 64)()
        n = lib.libfive_tetflow_items(p, 0, buf, 64)
        self.inlet_flows = [buf[i] for i in range(min(n, 64))]
        n = lib.libfive_tetflow_items(p, 1, buf, 64)
        self.outlet_flows = [buf[i] for i in range(min(n, 64))]
        buf3 = (ctypes.c_double * 192)()
        n = lib.libfive_tetflow_items(p, 2, buf3, 192)
        self.wall_forces = [(buf3[3 * i], buf3[3 * i + 1], buf3[3 * i + 2]) for i in range(min(n, 64))]
        w = lib.libfive_tetflow_warning(p)
        self.warning = w.decode('utf-8', 'replace') if w else ''
        self.min_pressure, self.max_pressure = self._ranges['pressure']
        self._items = items
        self.body = self.domain = None          # (what the flow is around, and in: set by fluid_analysis)
        self.steps = []
        self.times = []

    def range(self, field='speed'):
        ''' (min, max) of a field over the fluid '''
        return self._ranges[field]

    def _flow_direction(self):
        ''' (the unit vector the fluid comes in along, the mean speed in mm/s): from the inlets that were given a `velocity`; None when none was '''
        total, n, speed = [0.0, 0.0, 0.0], 0, 0.0
        for c in getattr(self, '_conditions', None) or ():
            if isinstance(c, _Inlet) and any(c.direction):
                mag = math.sqrt(sum(v * v for v in c.direction))
                total = [t + v / mag for t, v in zip(total, c.direction)]
                speed += mag
                n += 1
        norm = math.sqrt(sum(t * t for t in total))
        if n and norm > 0:
            return tuple(t / norm for t in total), speed / n
        # (an inlet given a `speed=` goes along its own normal, which the script does not say: the way through is taken to be from the middle of the
        # inlets to the middle of the outlets, when the regions know their extent)
        def box_of(c):
            b = getattr(c.region, '_bounds', None)
            if b:
                return b
            try:
                from fieldes.stdlib.fields import find_extent
                return find_extent(c.region)
            except Exception:
                return None

        def middle(items):
            boxes = [box_of(c) for c in items]
            boxes = [b for b in boxes if b]
            if not boxes:
                return None
            return [sum((b[0][i] + b[1][i]) / 2 for b in boxes) / len(boxes) for i in range(3)]
        conditions = getattr(self, '_conditions', None) or ()
        inlets = [c for c in conditions if isinstance(c, _Inlet) and c.speed > 0]
        outlets = [c for c in conditions if type(c).__name__ == '_Outlet']
        a, b = middle(inlets), middle(outlets)
        if not inlets or a is None or b is None:
            return None
        way = [y - x for x, y in zip(a, b)]
        length = math.sqrt(sum(v * v for v in way))
        if length <= 0:
            return None
        return tuple(v / length for v in way), sum(c.speed for c in inlets) / len(inlets)

    def _lift_direction(self, d):
        ''' The unit vector across the flow that is most nearly Z (Y when the flow runs along Z) '''
        z = (0.0, 0.0, 1.0) if abs(d[2]) < 0.999 else (0.0, 1.0, 0.0)
        dot = sum(a * b for a, b in zip(z, d))
        l = [a - dot * b for a, b in zip(z, d)]
        norm = math.sqrt(sum(a * a for a in l))
        return tuple(a / norm for a in l)

    @property
    def drag_force(self):
        ''' The force of the flow on the walls along the flow (N): `wall_force` along the direction the inlets' velocity points in.  For a body in a stream
            whose domain has slip walls it is the body's drag.  None when no inlet was given a `velocity=` (the direction is not known) '''
        found = self._flow_direction()
        if found is None:
            return None
        return sum(f * d for f, d in zip(self.wall_force, found[0]))

    @property
    def lift_force(self):
        ''' The force of the flow on the walls across the flow (N), along the direction across it that is most nearly Z (Y for a flow along Z).
            None when no inlet was given a `velocity=` '''
        found = self._flow_direction()
        if found is None:
            return None
        return sum(f * d for f, d in zip(self.wall_force, self._lift_direction(found[0])))

    def _reference(self):
        ''' (dynamic pressure in MPa, reference area in mm^2): the pressure 1/2 rho U^2 of the mean inlet speed, and the area of the body's bounding box
            seen along the flow -- or None '''
        found = self._flow_direction()
        body = getattr(self, 'body', None)
        bounds = getattr(body, '_bounds', None) if body is not None else None
        if body is not None and not bounds:
            try:
                from fieldes.stdlib.fields import find_extent
                bounds = find_extent(body)
            except Exception:
                bounds = None
        if found is None or not bounds:
            return None
        (dx, dy, dz), speed = found
        a, b, c = (bounds[1][i] - bounds[0][i] for i in range(3))
        area = b * c * abs(dx) + a * c * abs(dy) + a * b * abs(dz)
        q = 0.5 * self.fluid.density * speed * speed
        return (q, area) if q > 0 and area > 0 else None

    @property
    def drag_coefficient(self):
        ''' drag_force / (1/2 rho U^2 A): U the mean speed of the inlets that were given a velocity, A the area of the body's bounding box seen along the flow
            (the frontal area, taken from the box: give the true one by dividing `drag_force` yourself).  None when it is not known '''
        ref, force = self._reference(), self.drag_force
        return None if ref is None or force is None else force / (ref[0] * ref[1])

    @property
    def lift_coefficient(self):
        ''' lift_force / (1/2 rho U^2 A), as drag_coefficient.  None when it is not known '''
        ref, force = self._reference(), self.lift_force
        return None if ref is None or force is None else force / (ref[0] * ref[1])

    def _read_steps(self):
        p = self._handle.ptr
        n = lib.libfive_tetflow_step_count(p)
        self.steps = [FluidStep(self, k) for k in range(n)]
        self.times = [s.time for s in self.steps if s.iteration is None]

    def streamlines(self, seeds=None, count=40, max_time=None, max_points=4000, backward=False, step=None):
        ''' The paths of particles carried by the flow: a list of lines, each a list of (x, y, z, speed, time)
            points, from `seeds` (a list of (x, y, z); by default `count` points spread over the inlets), by
            Runge-Kutta steps of half an element, for up to `max_time` seconds (by default the time it takes to
            cross the fluid three times) or `max_points` points, until the particle leaves the fluid.  backward=True
            follows the flow upstream; step= picks a step of a flow in time (the last one by default). '''
        p = self._handle.ptr
        if seeds is None:
            buf = (ctypes.c_double * (3 * max(1, int(count))))()
            n = lib.libfive_tetflow_inlet_seeds(p, int(count), buf)
            seeds = [(buf[3 * i], buf[3 * i + 1], buf[3 * i + 2]) for i in range(n)]
            if not seeds:
                raise FeaError('streamlines: the flow has no inlet to seed from: give seeds=[(x, y, z), ...]')
        seeds = [tuple(float(c) for c in s) for s in seeds]
        if not seeds:
            return []
        if max_time is None:
            b = getattr(self, '_bounds', None)
            size = max(hi - lo for lo, hi in zip(*b)) if b else self.hydraulic_diameter * 10
            speed = self.max_speed if step is None else self.steps[int(step)].max_speed
            max_time = 3.0 * size / max(speed, 1e-12)
        k = -1 if step is None else int(step)
        arr = (ctypes.c_double * (3 * len(seeds)))(*[c for s in seeds for c in s])
        counts = (ctypes.c_int * len(seeds))()
        cap = 5 * int(max_points) * len(seeds)
        out = (ctypes.c_double * cap)()
        need = lib.libfive_tetflow_streamlines(p, k, arr, len(seeds), float(max_time), int(max_points), 1 if backward else 0,
                                               out, cap, counts)
        if need > cap:
            out = (ctypes.c_double * need)()
            lib.libfive_tetflow_streamlines(p, k, arr, len(seeds), float(max_time), int(max_points), 1 if backward else 0,
                                            out, need, counts)
        lines, pos = [], 0
        for i in range(len(seeds)):
            m = counts[i]
            if m <= 0:
                continue
            lines.append([(out[pos + 5 * j], out[pos + 5 * j + 1], out[pos + 5 * j + 2], out[pos + 5 * j + 3], out[pos + 5 * j + 4])
                          for j in range(m)])
            pos += 5 * m
        return lines

    def _mesh_data(self):
        ''' What FielDes draws for "Elements": the mesh of the fluid and each tetrahedron's value of each field -- the speed of its mean velocity, the others the mean of its vertices' --
            None when there are more tetrahedra than are drawn (see element_mesh) '''
        cached = getattr(self, '_mesh_full', False)
        if cached is not False:
            return cached
        full = None
        if hasattr(lib, 'libfive_tetflow_element_values') and getattr(self, '_handle', None) is not None:
            p = self._handle.ptr
            full = _element_mesh(lambda b: lib.libfive_tetflow_counts(p, b), lambda v, t, f, ft: lib.libfive_tetflow_mesh(p, v, t, f, ft),
                                 lambda f, buf, n: lib.libfive_tetflow_element_values(p, f, buf, n), range(len(_FIELDS)), None)
            if full is None:
                self._elements_note = _elements_note(int(self.elements))
        self._mesh_full = full
        return full

    def _display(self):
        ''' What FielDes shows for the result stated on its own: the fluid coloured by the speed, with 40 streamlines
            from the inlets and particles moving along them drawn over it; a step per solver iteration (a steady
            flow) or per stored time (a flow in time), each with its fields and streamlines '''
        shown = getattr(self, '_shown', None)
        if shown is None:
            shown = {'_color_fields': [(name, _LABELS[name], getattr(self, name)) + tuple(self._ranges[name])
                                       for name in _FIELDS],
                     '_color_field_name': 'speed', '_flow_lines': self.streamlines(),
                     '_flow_range': tuple(self._ranges['speed']),
                     '_fea_mesh': self._mesh_data(), '_fea_elements_note': getattr(self, '_elements_note', '')}
            if len(self.steps) > 1:
                # (the last step is the result itself: its fields and its lines are the result's own, so that its elements are shown with it)
                last = len(self.steps) - 1
                shown['_color_steps'] = [{'label': s.label} if k == last else
                                         {'label': s.label,
                                          'channels': [(name, _LABELS[name], getattr(s, name)) + tuple(s._ranges[name])
                                                       for name in _FIELDS],
                                          'lines': self.streamlines(step=k)}
                                         for k, s in enumerate(self.steps)]
                shown['_color_step'] = last
            self._shown = shown
        out = colored(self.shape, self.speed, range=self._ranges['speed'], label=_LABELS['speed'], colormap='turbo')
        out.__dict__.update(shown)
        out._color_detail = float(getattr(self, 'element_size', 0) or 0)
        if self.body is None:
            return out
        # (the body the flow goes around, solid, with the fluid: as the flow optimisation shows its body)
        solid = Shape(lib.libfive_tree_copy(self.body.ptr))
        solid._bounds = getattr(self.shape, '_bounds', None)
        return [out, solid]

    def __repr__(self):
        if getattr(self, 'times', None):
            s = _tr('Flow in time: %s elements, %s nodes, %d steps to t = %.4g s (%.1f s to solve)\n'
                    '  at the end: flow in %.4g mm^3/s, out %.4g mm^3/s \u00b7 pressure drop %.4g MPa \u00b7 max speed %.4g mm/s\n'
                    '  Reynolds number %.0f \u00b7 %.1f elements across the passages') % (
                        '{:,}'.format(self.elements), '{:,}'.format(self.nodes), len(self.steps), self.times[-1], self.seconds,
                        self.inlet_flow, self.outlet_flow, self.pressure_drop, self.max_speed, self.reynolds, self.elements_across)
            if self.warning:
                s += '\n' + _tr('  warning: %s') % self.warning
            return s
        s = _tr('Flow analysis: %s elements, %s nodes (%d iterations, %.1f s%s)\n'
                '  flow in %.4g mm^3/s, out %.4g mm^3/s \u00b7 pressure drop %.4g MPa \u00b7 max speed %.4g mm/s\n'
                '  Reynolds number %.0f \u00b7 %.1f elements across the passages \u00b7 dissipation %.4g W') % (
                    '{:,}'.format(self.elements), '{:,}'.format(self.nodes), self.iterations, self.seconds,
                    '' if self.converged else _tr(', NOT converged'),
                    self.inlet_flow, self.outlet_flow, self.pressure_drop, self.max_speed, self.reynolds,
                    self.elements_across, self.dissipation)
        if self.warning:
            s += '\n' + _tr('  warning: %s') % self.warning
        return s


class FluidStep:
    ''' One step of a flow: after a solver iteration of a steady solve (.iteration, 0 the Stokes start; .time
        None), or at a stored time of a flow in time (.time in s; .iteration None).  The fields (speed, vx, vy,
        vz, pressure, total_pressure, shear_rate, vorticity) and the numbers (inlet_flow, outlet_flow,
        pressure_drop, max_speed, wall_force, dissipation, residual, converged) at that step; streamlines().
        Stated on its own, FielDes shows it as it shows the result. '''

    def __init__(self, result, k):
        p = result._handle.ptr
        self._handle = result._handle
        self._result = result
        self.index = k
        it = lib.libfive_tetflow_step_iteration(p, k)
        self.iteration = None if it < 0 else int(it)
        self.time = None if self.iteration is not None else lib.libfive_tetflow_step_time(p, k)
        self.label = (_tr('t = %.4g s') % self.time) if self.iteration is None else \
            (_tr('Stokes start') if self.iteration == 0 else _tr('iteration %d') % self.iteration)
        self._ranges = {}
        for i, name in enumerate(_FIELDS):
            setattr(self, name, field_shape(lib.libfive_tetflow_step_field(p, k, i)))
            self._ranges[name] = (lib.libfive_tetflow_step_field_min(p, k, i), lib.libfive_tetflow_step_field_max(p, k, i))
        stat = lambda w: lib.libfive_tetflow_step_stat(p, k, w)
        self.iterations = int(stat(3))
        self.residual = stat(4)
        self.inlet_flow = stat(6)
        self.outlet_flow = stat(7)
        self.pressure_drop = stat(9)
        self.max_speed = stat(10)
        self.dissipation = stat(11) * 1e-3
        self.wall_force = (stat(16), stat(17), stat(18))
        self.converged = bool(stat(19))

    def range(self, field='speed'):
        return self._ranges[field]

    def streamlines(self, seeds=None, count=40, max_time=None, max_points=4000, backward=False):
        ''' The streamlines of the flow at this time (see FluidResult.streamlines) '''
        return self._result.streamlines(seeds, count, max_time, max_points, backward, step=self.index)

    def _display(self):
        out = colored(self._result.shape, self.speed, range=self._ranges['speed'], label=_LABELS['speed'],
                      colormap='turbo')
        out._color_fields = [(name, _LABELS[name], getattr(self, name)) + tuple(self._ranges[name]) for name in _FIELDS]
        out._color_field_name = 'speed'
        out._flow_lines = self.streamlines()
        out._flow_range = tuple(self._ranges['speed'])
        out._color_detail = float(getattr(self._result, 'element_size', 0) or 0)
        return out

    def __repr__(self):
        return 'FluidStep(%s: max speed %.4g mm/s, pressure drop %.4g MPa, wall force (%.3g, %.3g, %.3g) N)' % (
            self.label, self.max_speed, self.pressure_drop, *self.wall_force)


def _problem(shape, conditions, fluid, element_size, bounds, gravity, stokes, max_iterations, relaxation, what):
    if not isinstance(shape, Shape):
        raise TypeError('{}: the fluid domain must be a Shape'.format(what))
    if not isinstance(fluid, Fluid):
        raise TypeError('{}: fluid must be a Fluid (water, air, oil, or Fluid(name, density, viscosity))'.format(what))
    if getattr(lib, 'libfive_tetflow_new', None) is None:
        raise FeaError('this FielDes library is too old for flow analysis')
    items = list(conditions if isinstance(conditions, (list, tuple)) else [conditions])
    lo, hi = bounds if bounds is not None else _bounds(shape)
    size = [hi[i] - lo[i] for i in range(3)]
    if element_size is None:
        element_size = max(size) / 40.0
    element_size = float(element_size)
    pad = 1e-6 * max(max(size), 1e-9)
    region = libfive_region_t()
    for i, axis in enumerate((region.X, region.Y, region.Z)):
        axis.lower, axis.upper = lo[i] - pad, hi[i] + pad
    ptr = lib.libfive_tetflow_new(shape.ptr, region, element_size, fluid.density, fluid.viscosity)
    handle = _Handle(ptr)
    n_in = 0
    for b in items:
        if isinstance(b, _Inlet):
            lib.libfive_tetflow_add_inlet(ptr, b.region.ptr, b.direction[0], b.direction[1], b.direction[2], b.speed,
                                          b.flow_rate, _PROFILES[b.profile])
            n_in += 1
        elif isinstance(b, _Outlet):
            lib.libfive_tetflow_add_outlet(ptr, b.region.ptr, b.pressure)
        elif isinstance(b, _Wall):
            lib.libfive_tetflow_add_wall(ptr, b.region.ptr, *b.velocity)
        elif isinstance(b, _Slip):
            lib.libfive_tetflow_add_slip(ptr, b.region.ptr)
        else:
            raise TypeError('{}: the conditions are inlet(...), outlet(...), wall(...) and slip(...) items'.format(what))
    n_out = sum(isinstance(b, _Outlet) for b in items)
    moving = any(isinstance(b, _Wall) and any(abs(v) > 0 for v in b.velocity) for b in items)
    if n_in and not n_out and not moving:
        raise FeaError('{}: the fluid comes in at an inlet and has nowhere to leave -- give the flow an outlet (outlets=[outlet(...)]): with no '
                       'outlet and no moving wall the pressure has nothing to settle against, and the solver would only go round'.format(what))
    if not n_in and not n_out and not moving:
        raise FeaError('{}: nothing drives the flow -- it needs an inlet, an outlet (at another pressure) or a moving wall'.format(what))
    if gravity is not None:
        g = tuple(float(x) for x in gravity)
        if len(g) != 3:
            raise ValueError('{}: gravity must be (gx, gy, gz) in mm/s^2'.format(what))
        lib.libfive_tetflow_set_gravity(ptr, *g)
    lib.libfive_tetflow_set_options(ptr, 1 if stokes else 0, int(max_iterations), float(relaxation), 0, 0, 0.0, 1)
    return handle, element_size, (lo, hi), items


_cache = OrderedDict()


@_with_conditions
def fluid_analysis(body, domain, inlets, outlets, boundaries, fluid=water, element_size=None, bounds=None, gravity=None, stokes=False,
                   tolerance=1e-5, relaxation=1.0, cache=True, time=None, store_every=1):
    ''' Laminar flow of `fluid` around `body` (a Shape, the solid the flow goes around) in `domain` (a Shape whose inside is the
        fluid, the body's place in it too: the fluid is the domain with the body cut out) with the boundary conditions, the same
        inputs of topology_optimization for a body in a flow, one for each kind (each one item or a list, [] for none):
        inlets    inlet(...) items -- where the fluid comes in (as many as the flow has)
        outlets   outlet(...) items -- where it leaves (one is needed, or a moving wall)
        boundaries  wall(...) items -- moving walls (every surface in no region is a wall at rest) -- and slip(...) items -- symmetry planes
                    and frictionless walls: one or both, in one list
        (see the module's description) -- the steady flow, or the flow in time (time=).  Each condition
        is a model of its own, drawn on `domain`.  Every surface in no region is a wall at rest: the body's is, unless a wall(...) moves it.

        element_size   mm (default: 40 elements along the longest side); the passages should be
                       four elements across or more
        gravity        (gx, gy, gz) in mm/s^2, a body force on the fluid (none by default)
        stokes         True: creeping flow, the convection left out (linear: one solve)
        time           (duration, step) in seconds: the flow in TIME instead of the steady flow -- from
                       the Stokes flow at t = 0 (an impulsive start) by steps of `step` seconds to
                       `duration` (backward Euler; a step of about an element crossing, element_size /
                       speed, keeps it accurate).  The result's .steps hold every store_every-th step
                       (its time, fields and numbers) instead of the steady solve's iterations, the
                       result's own fields are the last step's, and the result card steps through them.
                       A wake that sheds vortices needs this: it has no steady state
        tolerance      the relative residual of the discrete equations at which to stop
        bounds         ((x0, y0, z0), (x1, y1, z1)) of the domain (found if not given)

        Returns a FluidResult: the fields (speed, pressure, ...), the flows and the pressure drop,
        the wall force (on every wall, the body's: its drag is the part along the flow), streamlines(), .body and .domain.  An unchanged problem is cached (as a static analysis is).
        Raises FeaError when the problem cannot be solved as given (a region that touches no
        surface, no outlet and no moving wall, a flow that does not converge). '''
    max_iterations = 60         # (the nonlinear iterations at most: a flow that has not settled by then is unsteady or its mesh too coarse, which the error says)
    conditions = _flow_conditions(inlets, outlets, boundaries, 'fluid_analysis')
    if not isinstance(body, Shape) or not isinstance(domain, Shape):
        raise TypeError('fluid_analysis: the body and the domain must be Shapes (the flow goes around the body, inside the domain)')
    _use_conditions(conditions, domain)
    around = _difference(domain, body)
    around._bounds = bounds if bounds is not None else _bounds(domain)
    result = _fluid_analysis(around, conditions, fluid, element_size, bounds, gravity, stokes, max_iterations, tolerance, relaxation, cache,
                             time, store_every)
    result.body, result.domain = body, domain
    result._conditions = conditions
    return result


def _fluid_analysis(shape, conditions, fluid=water, element_size=None, bounds=None, gravity=None, stokes=False,
                    max_iterations=60, tolerance=1e-5, relaxation=1.0, cache=True, time=None, store_every=1):
    if time is not None:
        try:
            duration, dt = float(time[0]), float(time[1])
        except (TypeError, IndexError, ValueError):
            raise ValueError('fluid_analysis: time=(duration, step) in seconds')
        if not (duration > 0 and dt > 0):
            raise ValueError('fluid_analysis: the duration and the time step must be positive')
        nsteps = max(1, int(round(duration / dt)))
        time = (nsteps * dt, dt)
    ekey = problem_key('flow', mesh=2, shape=shape, part_bounds=getattr(shape, '_bounds', None), conditions=conditions,
                       fluid=fluid, element_size=element_size, bounds=bounds, gravity=gravity, stokes=bool(stokes),
                       max_iterations=int(max_iterations), tolerance=float(tolerance),
                       relaxation=float(relaxation), time=time, store_every=int(store_every)) if cache else None
    if ekey is not None and ekey in _cache:
        _cache.move_to_end(ekey)
        cached = _cache[ekey]
        if cached.shape is shape:
            return cached
        r = object.__new__(FluidResult)      # the same problem from an identical shape
        r.__dict__.update(cached.__dict__)
        r.shape = shape
        return r
    handle, element_size, (lo, hi), items = _problem(shape, conditions, fluid, element_size, bounds, gravity, stokes,
                                                     max_iterations, relaxation, 'fluid_analysis')
    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load('tetflow', ekey, 'flow analysis')
    if loaded is not None:
        result = FluidResult(_Handle(loaded[0]), shape, fluid, items)
        result._bounds = (lo, hi)
        result.element_size = element_size
        result._read_steps()
        _cache[ekey] = result
        return result
    ptr = handle.ptr
    key = None
    if not lib.libfive_tetflow_prepare(ptr):
        raise FeaError('fluid_analysis: ' + lib.libfive_tetflow_message(ptr).decode('utf-8', 'replace'))
    if cache:
        try:
            key = (lib.libfive_tetflow_hash(ptr), float(tolerance), time, int(store_every),
                   tuple((type(b).__name__, shape_key(b.region)) +
                         tuple(sorted((k2, v) for k2, v in vars(b).items() if k2 != 'region'))
                         for b in items))
        except Uncacheable:
            key = None
        if key is not None and key in _cache:
            _cache.move_to_end(key)
            cached = _cache[key]
            if cached.shape is shape:
                return cached
            r = object.__new__(FluidResult)
            r.__dict__.update(cached.__dict__)
            r.shape = shape
            return r
    if ekey is not None:
        lib.libfive_tetflow_set_salt(ptr, result_cache.salt('tetflow', ekey))
    if time is not None:
        ok = lib.libfive_tetflow_solve_transient(ptr, time[1], int(round(time[0] / time[1])), int(store_every),
                                                 float(tolerance))
    else:
        ok = lib.libfive_tetflow_solve(ptr, float(tolerance))
    if not ok:
        raise FeaError('fluid_analysis: ' + lib.libfive_tetflow_message(ptr).decode('utf-8', 'replace'))
    result = FluidResult(handle, shape, fluid, items)
    result._bounds = (lo, hi)
    result.element_size = element_size
    result._read_steps()
    result_cache.save('tetflow', ekey, ptr, 'flow analysis')
    if key is not None:
        _cache[key] = result
    if ekey is not None:
        _cache[ekey] = result
    while len(_cache) > 8:
        _cache.popitem(last=False)
    return result


################################################################################
# Shape optimisation of a body in the flow

def _outside_body(lines, level):
    ''' The parts of the streamlines outside the body: in the optimiser's flow the solid is a friction and a
        little fluid creeps through it, so the lines are cut where they would enter it (the real flow of the
        final step has no such lines) '''
    from fieldes.stdlib.fields import evaluate
    points = [(p[0], p[1], p[2]) for line in lines for p in line]
    if not points:
        return lines
    inside = evaluate(level, points)
    out, i = [], 0
    for line in lines:
        run = []
        for p in line:
            if inside[i] > 0:
                if len(run) >= 2:
                    out.append(run)
                run = []
            else:
                run.append(p)
            i += 1
        if len(run) >= 2:
            out.append(run)
    return out


class FlowTopologyResult(FieldResult):
    ''' The result of topology_optimization() for a body in a flow (the result of fluid_analysis): the body made best in the flow.
        .level          a field (mm, positive inside the body, its boundary the zero level: a smooth
                        level set on the flow's mesh); .levels the same after each iteration (the first
                        the body as given, the last the final body)
        .shape(iteration=None)  the optimised body, or the body after an iteration
        .fluid_shape(iteration=None)  the fluid around it (the domain with the body taken out)
        .drag, .lift    N, per iteration, as the optimiser's model sees them (the body a friction in the
                        flow), the last the final body's; .flow_direction, .lift_direction
        .flow           the REAL flow around the final body (a fluid_analysis of fluid_shape() with the
                        same conditions, the body a wall at rest): fields, numbers, streamlines();
                        .real_drag, .real_lift its force on the walls along the two directions: the MEAN over several places of the mesh lattice (a mesh meets a body at
                        some place and the force depends on it, by 5 to 25 % for a body a few elements thick), .real_drag_sd the standard deviation of one place,
                        .real_drags the forces of the places, `best.drag_force` the mean as well
        .model_flow     the optimiser's own flow (the body as a friction), .model_flow.steps[k] the flow
                        around the body of iteration k
        .body, .domain, .volume (mm^3 of the final body), .iterations (accepted), .seconds,
        .steps_back (steps undone because they raised the objective), .stopped (why the run ended)
        .values         each thing the constraints asked, as the final body has it in the real flow; .feasible, .goal, .table(), .history (the
                        objective per iteration, as the optimiser's model had it), .volume_fraction (the share of the body's own volume the design
                        uses), .body_volume (mm^3 of the body it started as), .pieces (in how many pieces the body is); the numbers of the real flow -- `best.drag_force`, `best.pressure_drop`,
                        `best.speed` -- are the result's own
        Stated on its own, FielDes shows the body in the flow: the fluid coloured by the speed with
        streamlines and particles, the body solid; the result card steps through the iterations (the
        body as it was after each one, and the flow around it). '''

    def __init__(self, model_flow, body, domain, level, levels, drag, lift, directions, settings,
                 element_size, bounds, seconds):
        self.model_flow = model_flow
        self.flow = None                     # (the real flow: set by topology_optimization)
        self.real_drag = self.real_lift = None
        self.body = body
        self.domain = domain
        self.level = level
        self.levels = levels
        self.drag = drag
        self.lift = lift
        self.flow_direction, self.lift_direction = directions
        self.settings = settings
        self.element_size = element_size
        self.bounds = bounds
        self.iterations = max(0, len(drag) - 1)
        self.seconds = seconds
        self.volume = lib.libfive_tetflow_stat(model_flow._handle.ptr, 22) if getattr(lib, 'libfive_tetflow_stat', None) else 0.0
        # how it ended: the steps taken back (each halved the step), and why it stopped
        self.steps_back = int(lib.libfive_tetflow_stat(model_flow._handle.ptr, 23))
        self.stopped = ('the iteration limit', 'converged: the objective stopped improving',
                        'converged: no step lowers the objective any more', 'no sensitivity left')[
                            max(0, min(3, int(lib.libfive_tetflow_stat(model_flow._handle.ptr, 24))))]

    def shape(self, iteration=None):
        ''' The body: where the level set is positive (its boundary the zero level, a fraction of an element
            exact); iteration=k the body after iteration k (0 the body as given) '''
        d = self.level if iteration is None else self.levels[iteration]
        # (inside the fluid domain: the level set is a field on its mesh, continued outside it by its nearest value)
        out = (-d).max(self.domain)
        out._bounds = self.bounds
        return carry_locks(out, self.body)

    def fluid_shape(self, iteration=None):
        ''' The fluid around the body: the domain with the body taken out '''
        out = _difference(self.domain, self.shape(iteration))
        out._bounds = self.bounds
        return out

    def _display(self):
        ''' What FielDes shows for the result stated on its own: the fluid around the final body coloured by the
            speed, with streamlines and particles, and the body itself; a step per iteration with the body of
            that iteration and the flow around it '''
        shown = getattr(self, '_shown', None)
        if shown is None:
            model = self.model_flow
            real = self.flow if self.flow is not None else model
            n = len(self.levels)
            fluid_steps, body_steps = [], []
            for k in range(n):
                label = _tr('the body as given') if k == 0 else _tr('iteration %d') % k
                last = k == n - 1
                step = {'label': label}
                if last:
                    pass        # (the final body: the real flow around it is what the result itself shows, with its elements: no fields of its own for the step)
                elif k < len(model.steps):
                    st = model.steps[k]
                    step['channels'] = [(name, _LABELS[name], getattr(st, name)) + tuple(st._ranges[name]) for name in _FIELDS]
                    step['lines'] = _outside_body(model.streamlines(step=k), self.levels[k])
                if not last:
                    step['shape'] = self.fluid_shape(iteration=k)
                fluid_steps.append(step)
                body_steps.append({'label': label} if last else {'label': label, 'shape': self.shape(iteration=k)})
            shown = {'fluid': self.fluid_shape(), 'body': self.shape(),
                     '_color_fields': [(name, _LABELS[name], getattr(real, name)) + tuple(real._ranges[name]) for name in _FIELDS],
                     '_flow_lines': real.streamlines(), '_flow_range': tuple(real._ranges['speed']),
                     'fluid_steps': fluid_steps, 'body_steps': body_steps, 'real': real,
                     '_fea_mesh': real._mesh_data(), '_fea_elements_note': getattr(real, '_elements_note', '')}
            self._shown = shown
        real = shown['real']
        fluid = colored(shown['fluid'], real.speed, range=real._ranges['speed'], label=_LABELS['speed'], colormap='turbo')
        fluid._color_fields = shown['_color_fields']
        fluid._color_field_name = 'speed'
        fluid._flow_lines = shown['_flow_lines']
        fluid._flow_range = shown['_flow_range']
        fluid._fea_mesh = shown['_fea_mesh']
        fluid._fea_elements_note = shown['_fea_elements_note']
        fluid._color_detail = float(self.element_size or 0)
        body = Shape(lib.libfive_tree_copy(shown['body'].ptr))
        body._bounds = self.bounds
        if len(shown['fluid_steps']) > 1:
            fluid._color_steps = shown['fluid_steps']
            fluid._color_step = len(shown['fluid_steps']) - 1
            body._color_steps = shown['body_steps']
            body._color_step = len(shown['body_steps']) - 1
        return [fluid, body]

    table = TopologyResult.table

    def _set_problem(self, analyses, outputs, program, bases, outs, goal, body_volume):
        self._analyses, self._outputs, self._program = analyses, outputs, program
        self._bases, self._outs, self.goal = bases, outs, goal
        self.body_volume = body_volume
        self.analysis = self.flow
        sign = -1.0 if goal == 'maximize' else 1.0
        self.history = [sign * v for v in outs[0]] if outs else []
        fraction = bases[program.measures.index(('fraction',))] if bases else []
        self.volume_fraction = fraction[-1] if fraction else None
        self.values = OrderedDict()
        self.feasible = None

    def _verify_into(self):
        ''' The numbers that were asked for, as the final body has them in the real flow '''
        from fieldes.stdlib import optimization_measures as measures
        share = self.volume / self.body_volume if self.body_volume > 0 else None      # (what the cut body keeps of the body, exactly)
        self._part_share = share

        def leaf(key):
            if key[0] == 'fraction':
                return share
            return measures.true_value(key, [self._mean_flow])
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

    @property
    def drag_coefficient(self):
        ''' The drag as a coefficient, of the force the result says (`drag_force`: the mean over the places of the mesh lattice), with the reference of the real flow
            (1/2 rho U^2 and the frontal area of the body's box).  None when it is not known '''
        flow, force = self.__dict__.get('flow'), self.__dict__.get('drag_force')
        ref = flow._reference() if flow is not None else None
        return None if ref is None or force is None else force / (ref[0] * ref[1])

    @property
    def lift_coefficient(self):
        ''' The lift as a coefficient, of the force the result says (`lift_force`: the mean over the places), as `drag_coefficient` '''
        flow, force = self.__dict__.get('flow'), self.__dict__.get('lift_force')
        ref = flow._reference() if flow is not None else None
        return None if ref is None or force is None else force / (ref[0] * ref[1])

    def __getattr__(self, name):
        # (what the real flow around the final body has: its numbers and fields -- `best.drag_force`, `best.pressure_drop`, `best.speed`)
        if name.startswith('_') or self.__dict__.get('_forwarding'):
            raise AttributeError(name)
        flow = self.__dict__.get('flow')
        if flow is not None and (name in flow.__dict__ or hasattr(type(flow), name)):
            self.__dict__['_forwarding'] = True
            try:
                return getattr(flow, name)
            finally:
                self.__dict__['_forwarding'] = False
        return FieldResult.__getattr__(self, name)

    def __repr__(self):
        share = 100 * (self.volume_fraction or 0.0)
        if self.goal == 'satisfy':
            lines = [_tr('Topology optimization of a flow: %.0f %% of the body kept (%d iterations, %s; %.1f s)') % (share, self.iterations, self.stopped, self.seconds)]
        else:
            h = self.history
            lines = [_tr('Topology optimization of a flow: %s %s %.4g -> %.4g, %.0f %% of the body kept (%d iterations, %s; %.1f s)') % (
                self.goal, self._outputs[0].text, h[0] if h else 0, h[-1] if h else 0, share, self.iterations, self.stopped, self.seconds)]
        if len(self._outputs) > 1 or self.values:
            lines.append(self.table())
        drags = getattr(self, 'real_drags', None)
        if drags and len(drags) > 1 and self.real_drag:
            spread = 100.0 * (max(drags) - min(drags)) / abs(self.real_drag)
            lines.append(_tr('  The real flow around the final body was solved on the mesh lattice at %d places: its drag is %.4g N (the mean; the places differ by %.1f %%, the mean is good to about %.1f %%).') % (
                len(drags), self.real_drag, spread, 100.0 * self.real_drag_sd / abs(self.real_drag) / math.sqrt(len(drags))))
            if spread > 5.0:
                lines.append(_tr('  A body this thin is not resolved by the mesh: the places differ by more than 5 percent, so one mesh alone is no better than that; a smaller element_size narrows it.'))
        if getattr(self, 'pieces', 1) > 1:
            lines.append(_tr('  The body is in %d pieces: the small ones are volume parked where the optimiser\'s friction lets the flow through (a piece of an element or two), so it charges them little drag and the real flow charges more; a finer element_size holds the volume in one body.') % self.pieces)
        if self.feasible is False:
            lines.append(_tr('  A constraint is broken by the optimized part (the design is the optimiser\'s own; the part is cut from it).'))
        return '\n'.join(lines)


def _phase_forces(result, items, fluid, element_size, bounds, stokes, cache, d, l):
    """ The force of the real flow around the final body along the flow (d) and across it (l), on the mesh lattice at several places.  A mesh of tetrahedra on a lattice meets a
        body at some place, and the force on the body depends on it -- by 5 to 25 % for a body a few elements thick, measured -- so one mesh is not an answer: the flow is solved
        on the lattice moved against the body (the region of the mesh grown by a part of an element on its lower side, in each direction: a low-discrepancy sequence over one period
        of the lattice, half an element), and the mean and the spread are what the result reports.  The first place is the domain's own region (the flow `result.flow`) """
    flow = result.flow
    wf = flow.wall_force
    drags = [sum(wf[i] * d[i] for i in range(3))]
    lifts = [sum(wf[i] * l[i] for i in range(3))]
    n = int(getattr(flow, 'elements', 0) or 0)
    count = 8 if n <= 100000 else (4 if n <= 300000 else (2 if n <= 1000000 else 1))
    lo, hi = bounds
    h = float(element_size)
    alpha = (0.8191725134, 0.6710436067, 0.5497004779)           # (the generalised golden ratio sequence for three dimensions)
    shape = result.fluid_shape()
    for k in range(1, count):
        shift = [((0.5 + alpha[a] * k) % 1.0) * 0.5 * h for a in range(3)]
        moved = (tuple(lo[a] - shift[a] for a in range(3)), tuple(hi))
        f = _fluid_analysis(shape, items, fluid=fluid, element_size=element_size, bounds=moved, stokes=stokes, cache=cache)
        w = f.wall_force
        drags.append(sum(w[i] * d[i] for i in range(3)))
        lifts.append(sum(w[i] * l[i] for i in range(3)))
    return drags, lifts


def _mean_sd(values):
    mean = sum(values) / len(values)
    sd = math.sqrt(sum((v - mean) ** 2 for v in values) / (len(values) - 1)) if len(values) > 1 else 0.0
    return mean, sd


_density_cache = OrderedDict()


def topology_optimization_flow(analysis, constraints=(), iterations=None, filter_radius=None, keep=None, avoid=None, grow=False, grow_limit=None,
                               extrude=None, penalty=3.0, sharpness=16.0, symmetry='auto', tolerance=1e-5, cache=True):
    ''' topology_optimization() for a body in a flow (see fea.py): `analysis` is the result of fluid_analysis(body, domain, inlets, outlets, boundaries).
        The body is the design it starts from, the fluid domain is its space; the numbers the constraints can read are drag_force and lift_force (the
        force of the flow on the body along the flow and across it, N), volume_fraction (of the body's own volume) and volume.  The flow of every
        iteration is solved once, with one adjoint solve for each force the constraints read; the density of each tetrahedron is the friction the fluid
        feels there (1 solid, 0 fluid) '''
    from fieldes.stdlib import optimization_measures as measures
    from fieldes.stdlib.fea import _optimization_analyses
    iterations = 80 if iterations is None else iterations
    if getattr(lib, 'libfive_tetflow_optimize_density', None) is None:
        raise FeaError('this FielDes library is too old for the topology optimization of a flow')
    if not constraints:
        raise TypeError("topology_optimization: say what the design is for -- constraints=[minimize('drag_force'), 'volume_fraction <= 0.5', ...]")
    if grow or grow_limit is not None:
        raise ValueError('topology_optimization: a body in a flow is designed inside the fluid domain it is in -- the domain is its space, grow does not apply')
    if penalty != 3.0 or tolerance != 1e-5:
        raise ValueError("topology_optimization: penalty and tolerance belong to the structural optimiser -- a flow has neither")
    analyses = _optimization_analyses(analysis, 'topology_optimization')
    if len(analyses) != 1:
        raise FeaError('topology_optimization: a body in a flow is designed for one flow analysis (%d were given)' % len(analyses))
    a = analyses[0]
    flow = a.result
    body, domain = flow.body, flow.domain
    if not isinstance(body, Shape) or not isinstance(domain, Shape):
        raise FeaError('topology_optimization: the flow analysis needs its body and its domain -- fluid_analysis(body, domain, inlets, outlets, boundaries)')
    if a.arguments.get('gravity') is not None:
        raise FeaError("topology_optimization: gravity isn't supported in the optimisation of a flow")
    if a.arguments.get('time') is not None:
        raise FeaError('topology_optimization: a flow in time (time=) is not optimised -- the steady flow is')
    fluid = a.arguments['fluid']
    element_size = a.arguments.get('element_size')
    bounds = a.arguments.get('bounds')
    stokes = bool(a.arguments.get('stokes', False))
    conditions = list(flow._conditions)
    found = flow._flow_direction()
    if found is None:
        raise FeaError('topology_optimization: the direction of the flow is not known -- give an inlet a velocity=(vx, vy, vz): drag and lift are along it and across it')
    fdir = tuple(float(v) for v in found[0])
    ldir = tuple(float(v) for v in flow._lift_direction(fdir))
    _use_conditions(conditions, domain)
    keep = [] if keep is None else list(keep if isinstance(keep, (list, tuple)) else [keep])
    avoid = [] if avoid is None else list(avoid if isinstance(avoid, (list, tuple)) else [avoid])
    keep = keep + keep_regions(body)
    axes = {None: -1, 'x': 0, 'y': 1, 'z': 2}
    if extrude not in axes:
        raise ValueError("extrude is None, 'x', 'y' or 'z'")
    mirrors = _symmetry_request(symmetry, 'tet', 'topology_optimization')
    if isinstance(constraints, str) or callable(constraints):
        constraints = [constraints]

    def baseline_leaf(key):
        return 1.0 if key[0] == 'fraction' else measures.true_value(key, [flow])

    def baseline(node):
        return measures.evaluate(node, baseline_leaf)

    outputs, goal = measures.outputs_of(constraints, analyses, baseline)
    program = measures.Program(outputs)
    if ('fraction',) not in program.measures:
        program.measures.append(('fraction',))                      # (the share of the body the design uses is always kept: the result reports it)
    ekey = problem_key('flow_topology_density', mesh=2, body=body, domain=domain, part_bounds=getattr(domain, '_bounds', None), conditions=conditions,
                       fluid=fluid, program=program.key(), directions=(fdir, ldir), element_size=element_size, bounds=bounds, stokes=stokes,
                       keep=keep, avoid=avoid, iterations=int(iterations), filter_radius=filter_radius, extrude=extrude, sharpness=float(sharpness),
                       symmetry=mirrors[2], algorithm=4) if cache else None      # (bumped when the optimiser changes -- 3: linear friction, smooth sharpening, the mesh mirrored about the planes of symmetry; 4: the design kept exactly symmetric, the flows started from the last flow and solved to 1e-5, the real drag the mean over the places of the lattice)
    if ekey is not None and ekey in _density_cache:
        _density_cache.move_to_end(ekey)
        cached = _density_cache[ekey]
        if cached.body is body and cached.domain is domain:
            return cached
        r = object.__new__(FlowTopologyResult)
        r.__dict__.update(cached.__dict__)
        r.body, r.domain = body, domain
        return r
    handle, element_size, (lo, hi), items = _problem(domain, conditions, fluid, element_size, bounds, None, stokes, 60, 1.0, 'topology_optimization')
    settings = {'iterations': int(iterations), 'extrude': axes[extrude], 'darcy': 0.1, 'filter_radius': float(filter_radius or 0.0),
                'sharpness': float(sharpness)}

    def finish(handle, ptr, seconds, symmetric=None, saved=None):
        ''' The result from a solved problem (just optimised, or read back from its file: `saved`, what the file says besides) '''
        level = field_shape(lib.libfive_tetflow_level(ptr))
        buf = (ctypes.c_double * 1000)()
        m = lib.libfive_tetflow_history(ptr, 0, buf, 1000)
        drag = [buf[i] for i in range(min(m, 1000))]
        m = lib.libfive_tetflow_history(ptr, 1, buf, 1000)
        lift = [buf[i] for i in range(min(m, 1000))]
        levels = []
        for k in range(len(drag)):
            p = lib.libfive_tetflow_level_at(ptr, k)
            if not p:
                break
            levels.append(Shape(p))
        d3 = (ctypes.c_double * 3)()
        lib.libfive_tetflow_direction(ptr, 0, d3)
        used_flow = (d3[0], d3[1], d3[2])
        lib.libfive_tetflow_direction(ptr, 1, d3)
        used_lift = (d3[0], d3[1], d3[2])
        model = FluidResult(handle, domain, fluid, items)
        model._bounds = (lo, hi)
        model.element_size = element_size
        model._read_steps()
        result = FlowTopologyResult(model, body, domain, level, levels, drag, lift, (used_flow, used_lift), settings, element_size, (lo, hi), seconds)
        # the real flow around the final body: the body a wall at rest, the same conditions (cached like any flow)
        result.flow = _fluid_analysis(result.fluid_shape(), items, fluid=fluid, element_size=element_size, bounds=(lo, hi), stokes=stokes, cache=cache)
        # (what fluid_analysis tells its result, so that its drag_force and lift_force can be read: the flow's direction is the inlets')
        result.flow.body, result.flow.domain, result.flow._conditions = result.shape(), domain, conditions
        drags, lifts = _phase_forces(result, items, fluid, element_size, (lo, hi), stokes, cache, used_flow, used_lift)
        result.real_drags, result.real_lifts = drags, lifts
        result.real_drag, result.real_drag_sd = _mean_sd(drags)
        result.real_lift, result.real_lift_sd = _mean_sd(lifts)
        result.drag_force, result.lift_force = result.real_drag, result.real_lift         # (what `best.drag_force` says: the mean over the places, not the one mesh)
        result._mean_flow = types.SimpleNamespace(drag_force=result.real_drag, lift_force=result.real_lift)
        if saved is not None:
            bases, outs, body_volume = saved['bases'], saved['outputs'], float(saved['body_volume'])
            pieces = int(saved.get('pieces', 1))
        else:
            bases, outs = [], []
            for j in range(len(program.measures)):
                n = lib.libfive_tetflow_measure_history(ptr, 0, j, buf, 1000)
                bases.append([buf[i] for i in range(n)])
            for j in range(len(outputs)):
                n = lib.libfive_tetflow_measure_history(ptr, 1, j, buf, 1000)
                outs.append([buf[i] for i in range(n)])
            body_volume = float(lib.libfive_tetflow_stat(ptr, 25))
            pieces = int(lib.libfive_tetflow_stat(ptr, 27))
        result._set_problem(analyses, outputs, program, bases, outs, goal, body_volume)
        result.pieces = pieces
        if symmetric is None:
            symmetric = {}
            probe = getattr(lib, 'libfive_tetflow_mirror', None)
            for k, name in enumerate('xyz'):
                at = ctypes.c_double()
                how = probe(ptr, k, ctypes.byref(at)) if probe else 0
                if how:
                    symmetric[name] = [float(at.value), int(how)]
        found_planes = ['%s = %.4g' % (n, v[0]) for n, v in symmetric.items() if v[1] == 2]
        if found_planes:
            print('topology_optimization: the domain, the body and the boundary conditions are symmetric about %s: the design is kept symmetric '
                  '(symmetry=None leaves it alone).' % ' and '.join(found_planes))
        result.symmetry = {n: v[0] for n, v in symmetric.items()}
        result._symmetry_info = symmetric
        result._verify_into()
        return result

    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load('tetflow', ekey, 'flow topology optimization')
    if loaded is not None and loaded[1].get('measures'):
        result = finish(_Handle(loaded[0]), loaded[0], float(loaded[1].get('seconds', 0)), dict(loaded[1].get('symmetry') or {}), loaded[1]['measures'])
        _density_cache[ekey] = result
        return result
    ptr = handle.ptr
    if not lib.libfive_tetflow_prepare(ptr):
        raise FeaError('topology_optimization: ' + lib.libfive_tetflow_message(ptr).decode('utf-8', 'replace'))
    keep_arr = (ctypes.c_void_p * max(1, len(keep)))(*[_shape(r, 'keep').ptr for r in keep])
    avoid_arr = (ctypes.c_void_p * max(1, len(avoid)))(*[_shape(r, 'avoid').ptr for r in avoid])
    if mirrors[0] and getattr(lib, 'libfive_tetflow_set_symmetry_auto', None):
        lib.libfive_tetflow_set_symmetry_auto(ptr, 1)
    for axis, at in mirrors[1]:
        lib.libfive_tetflow_add_mirror(ptr, axis, 0.0 if at is None else float(at), 1 if at is None else 0)
    if ekey is not None:
        lib.libfive_tetflow_set_salt(ptr, result_cache.salt('tetflow', ekey))
    lib.libfive_tetflow_opt_clear(ptr)
    for key in program.measures:
        lib.libfive_tetflow_opt_measure(ptr, program.flow_measure(key))
    for code, value, index in program.ops:
        lib.libfive_tetflow_opt_op(ptr, int(code), float(value), int(index))
    lib.libfive_tetflow_opt_start(ptr, (outputs[0].text or 'constraints').encode('utf-8'), -1.0 if goal == 'maximize' else 1.0, float(sharpness))
    t0 = time.time()
    ok = lib.libfive_tetflow_optimize_density(ptr, body.ptr, None, fdir[0], fdir[1], fdir[2], ldir[0], ldir[1], ldir[2], settings['filter_radius'],
                                              settings['iterations'], keep_arr, len(keep), avoid_arr, len(avoid), settings['extrude'], settings['darcy'])
    if not ok:
        raise FeaError('topology_optimization: ' + lib.libfive_tetflow_message(ptr).decode('utf-8', 'replace'))
    result = finish(handle, ptr, time.time() - t0)
    meta = {'seconds': result.seconds, 'symmetry': result._symmetry_info,
            'measures': {'bases': result._bases, 'outputs': result._outs, 'body_volume': result.body_volume, 'pieces': result.pieces}}
    result_cache.save('tetflow', ekey, ptr, 'flow topology optimization', meta)
    if ekey is not None:
        _density_cache[ekey] = result
        while len(_density_cache) > 8:
            _density_cache.popitem(last=False)
    return result


FluidResult._as_field = lambda self: self.speed
FluidResult.fields = property(lambda self: OrderedDict((name, getattr(self, name)) for name in _FIELDS))
FlowTopologyResult._as_field = lambda self: self.shape()


def _flow_topology_fields(self):
    out = OrderedDict([('shape', self.shape()), ('level', self.level)])
    if self.flow is not None:
        out.update(self.flow.fields)           # (the flow around the final body: its speed, its pressure ...)
    return out


FlowTopologyResult.fields = property(_flow_topology_fields)
