'''
Fluid flow analysis of FielDes shapes: incompressible laminar flow, steady or in time.

    from fieldes import *

    pipe = cylinder_z(5, 60)                             # the FLUID domain: a shape whose inside is the fluid
    result = fluid_analysis(pipe, [
        inlet(box_exact((-6, -6, -1), (6, 6, 0.5)), flow_rate=2000, profile='developed'),   # mm^3/s, in
        outlet(box_exact((-6, -6, 59.5), (6, 6, 61)), pressure=0)],                           # MPa
        fluid=water, element_size=0.8)
    result                                               # the flow (FielDes: the fluid coloured by the speed,
                                                         # streamlines with moving particles; the result card)
    print(result.pressure_drop * 1e6, 'Pa')
    thicker = part + 0.02 * result.pressure              # results are fields like any other

The fluid domain is a shape, the fluid where its field is negative: the inside of a pipe or a
duct, a box with a part cut out of it (difference(box, part)).  The boundary conditions are regions
(shapes), on the surface of the fluid:
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
import ctypes
import time
from collections import OrderedDict

from fieldes.ffi import lib, libfive_region_t
from fieldes.shape import Shape
from fieldes.stdlib.excluded import keep_regions, carry_locks
from fieldes.stdlib.fea import FeaError, colored, _bounds, _shape
from fieldes.stdlib.csg import difference as _difference
from fieldes.stdlib.content_cache import Uncacheable, problem_key, shape_key
from fieldes.stdlib import result_cache

__all__ = ['Fluid', 'water', 'air', 'oil', 'glycerol', 'inlet', 'outlet', 'wall', 'slip', 'symmetry',
           'fluid_analysis', 'FluidResult', 'FluidStep', 'flow_topology_optimization', 'FlowTopologyResult']


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


class _Inlet:
    def __init__(self, region, direction, speed, flow_rate, profile):
        self.region, self.direction, self.speed, self.flow_rate, self.profile = region, direction, speed, flow_rate, profile


class _Outlet:
    def __init__(self, region, pressure):
        self.region, self.pressure = region, pressure


class _Wall:
    def __init__(self, region, velocity):
        self.region, self.velocity = region, velocity


class _Slip:
    def __init__(self, region):
        self.region = region


_PROFILES = {'uniform': 0, 'developed': 1}


def inlet(region, velocity=None, speed=None, flow_rate=None, profile='uniform'):
    ''' The fluid comes in through the surface inside `region` (a Shape).  One of:
        velocity   a vector (mm/s): the direction, and the mean speed over the inlet
        speed      a mean speed (mm/s) along the inlet's inward normal
        flow_rate  a flow rate (mm^3/s) along the inward normal
        profile    'uniform' (a plug, zero on the no-slip rim) or 'developed' (the fully developed
                   profile of the inlet's cross-section: parabolic in a round pipe)
        The speed or flow rate is matched exactly on the mesh (the flux through the inlet's triangles). '''
    region = _shape(region, 'inlet(region)')
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


def outlet(region, pressure=0.0):
    ''' The fluid leaves through the surface inside `region` at `pressure` (MPa, 0 by default; the
        pressure field is relative to it).  Put it where the flow leaves parallel to the walls. '''
    return _Outlet(_shape(region, 'outlet(region)'), _scalar(pressure, 'outlet: pressure'))


def wall(region, velocity=(0.0, 0.0, 0.0)):
    ''' A wall moving with `velocity` (mm/s; no-slip).  Surfaces in no region are walls at rest, so
        this is for moving walls, and for naming a wall whose force is wanted (wall_forces). '''
    v = tuple(_scalar(x, 'wall: velocity') for x in velocity)
    if len(v) != 3:
        raise ValueError('wall: velocity must be (vx, vy, vz)')
    return _Wall(_shape(region, 'wall(region)'), v)


def slip(region):
    ''' A symmetry plane or frictionless wall inside `region`: nothing flows through it, the fluid
        slides along it '''
    return _Slip(_shape(region, 'slip(region)'))


symmetry = slip

_FIELDS = ['speed', 'vx', 'vy', 'vz', 'pressure', 'total_pressure', 'shear_rate', 'vorticity']
_LABELS = {'speed': 'speed (mm/s)', 'vx': 'velocity x (mm/s)', 'vy': 'velocity y (mm/s)', 'vz': 'velocity z (mm/s)',
           'pressure': 'pressure (MPa)', 'total_pressure': 'total pressure (MPa)', 'shear_rate': 'shear rate (1/s)',
           'vorticity': 'vorticity (1/s)'}


class _Handle:
    ''' Owns a libfive_tetflow object (freed with the last result using it) '''
    def __init__(self, ptr):
        self.ptr = ptr

    def __del__(self):
        if self.ptr:
            lib.libfive_tetflow_delete(self.ptr)
            self.ptr = None


class FluidResult:
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
            setattr(self, name, Shape(lib.libfive_tetflow_field(p, i)))
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
        self.steps = []
        self.times = []

    def range(self, field='speed'):
        ''' (min, max) of a field over the fluid '''
        return self._ranges[field]

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

    def _display(self):
        ''' What FielDes shows for the result stated on its own: the fluid coloured by the speed, with 40 streamlines
            from the inlets and particles moving along them drawn over it; a step per solver iteration (a steady
            flow) or per stored time (a flow in time), each with its fields and streamlines '''
        shown = getattr(self, '_shown', None)
        if shown is None:
            shown = {'_color_fields': [(name, _LABELS[name], getattr(self, name)) + tuple(self._ranges[name])
                                       for name in _FIELDS],
                     '_color_field_name': 'speed', '_flow_lines': self.streamlines(),
                     '_flow_range': tuple(self._ranges['speed'])}
            if len(self.steps) > 1:
                shown['_color_steps'] = [{'label': s.label,
                                          'channels': [(name, _LABELS[name], getattr(s, name)) + tuple(s._ranges[name])
                                                       for name in _FIELDS],
                                          'lines': self.streamlines(step=k)}
                                         for k, s in enumerate(self.steps)]
                shown['_color_step'] = len(self.steps) - 1
            self._shown = shown
        out = colored(self.shape, self.speed, range=self._ranges['speed'], label=_LABELS['speed'], colormap='turbo')
        out.__dict__.update(shown)
        out._color_detail = float(getattr(self, 'element_size', 0) or 0)
        return out

    def __repr__(self):
        if getattr(self, 'times', None):
            s = ('Flow in time: {:,} elements, {:,} nodes, {} steps to t = {:.4g} s ({:.1f} s to solve)\n'
                 '  at the end: flow in {:.4g} mm^3/s, out {:.4g} mm^3/s \u00b7 pressure drop {:.4g} MPa \u00b7 max speed {:.4g} mm/s\n'
                 '  Reynolds number {:.0f} \u00b7 {:.1f} elements across the passages').format(
                     self.elements, self.nodes, len(self.steps), self.times[-1], self.seconds, self.inlet_flow, self.outlet_flow,
                     self.pressure_drop, self.max_speed, self.reynolds, self.elements_across)
            if self.warning:
                s += '\n  warning: ' + self.warning
            return s
        s = ('Flow analysis: {:,} elements, {:,} nodes ({} iterations, {:.1f} s{})\n'
             '  flow in {:.4g} mm^3/s, out {:.4g} mm^3/s · pressure drop {:.4g} MPa · max speed {:.4g} mm/s\n'
             '  Reynolds number {:.0f} · {:.1f} elements across the passages · dissipation {:.4g} W').format(
                 self.elements, self.nodes, self.iterations, self.seconds, '' if self.converged else ', NOT converged',
                 self.inlet_flow, self.outlet_flow, self.pressure_drop, self.max_speed, self.reynolds,
                 self.elements_across, self.dissipation)
        if self.warning:
            s += '\n  warning: ' + self.warning
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
        self.label = ('t = %.4g s' % self.time) if self.iteration is None else \
            ('Stokes start' if self.iteration == 0 else 'iteration %d' % self.iteration)
        self._ranges = {}
        for i, name in enumerate(_FIELDS):
            setattr(self, name, Shape(lib.libfive_tetflow_step_field(p, k, i)))
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
    if gravity is not None:
        g = tuple(float(x) for x in gravity)
        if len(g) != 3:
            raise ValueError('{}: gravity must be (gx, gy, gz) in mm/s^2'.format(what))
        lib.libfive_tetflow_set_gravity(ptr, *g)
    lib.libfive_tetflow_set_options(ptr, 1 if stokes else 0, int(max_iterations), float(relaxation), 0, 0, 0.0, 1)
    return handle, element_size, (lo, hi), items


_cache = OrderedDict()


def fluid_analysis(shape, conditions, fluid=water, element_size=None, bounds=None, gravity=None, stokes=False,
                   max_iterations=60, tolerance=1e-5, relaxation=1.0, cache=True, time=None, store_every=1):
    ''' Laminar flow of `fluid` through `shape` (the fluid domain: a Shape whose inside is the
        fluid) with the boundary conditions: inlet(...), outlet(...), wall(...), slip(...) items
        (see the module's description) -- the steady flow, or the flow in time (time=).

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
        max_iterations the nonlinear (Picard / Newton) iterations at most
        tolerance      the relative residual of the discrete equations at which to stop
        bounds         ((x0, y0, z0), (x1, y1, z1)) of the domain (found if not given)

        Returns a FluidResult: the fields (speed, pressure, ...), the flows and the pressure drop,
        the wall force, streamlines().  An unchanged problem is cached (as a static analysis is).
        Raises FeaError when the problem cannot be solved as given (a region that touches no
        surface, no outlet and no moving wall, a flow that does not converge). '''
    if time is not None:
        try:
            duration, dt = float(time[0]), float(time[1])
        except (TypeError, IndexError, ValueError):
            raise ValueError('fluid_analysis: time=(duration, step) in seconds')
        if not (duration > 0 and dt > 0):
            raise ValueError('fluid_analysis: the duration and the time step must be positive')
        nsteps = max(1, int(round(duration / dt)))
        time = (nsteps * dt, dt)
    ekey = problem_key('flow', shape=shape, part_bounds=getattr(shape, '_bounds', None), conditions=conditions,
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


class FlowTopologyResult:
    ''' The result of flow_topology_optimization(): the body made best in the flow.
        .level          a field (mm, positive inside the body, its boundary the zero level: a smooth
                        level set on the flow's mesh); .levels the same after each iteration (the first
                        the body as given, the last the final body)
        .shape(iteration=None)  the optimised body, or the body after an iteration
        .fluid_shape(iteration=None)  the fluid around it (the domain with the body taken out)
        .drag, .lift    N, per iteration, as the optimiser's model sees them (the body a friction in the
                        flow), the last the final body's; .flow_direction, .lift_direction
        .flow           the REAL flow around the final body (a fluid_analysis of fluid_shape() with the
                        same conditions, the body a wall at rest): fields, numbers, streamlines();
                        .real_drag, .real_lift its force on the walls along the two directions
        .model_flow     the optimiser's own flow (the body as a friction), .model_flow.steps[k] the flow
                        around the body of iteration k
        .body, .domain, .region, .volume (mm^3 of the final body), .iterations (accepted), .seconds,
        .steps_back (steps undone because they raised the objective), .stopped (why the run ended)
        Stated on its own, FielDes shows the body in the flow: the fluid coloured by the speed with
        streamlines and particles, the body solid; the result card steps through the iterations (the
        body as it was after each one, and the flow around it). '''

    def __init__(self, model_flow, body, domain, region, level, levels, drag, lift, directions, settings,
                 element_size, bounds, seconds):
        self.model_flow = model_flow
        self.flow = None                     # (the real flow: set by flow_topology_optimization)
        self.real_drag = self.real_lift = None
        self.body = body
        self.domain = domain
        self.region = region
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
        self.stopped = ('the iteration limit', 'converged: the boundary stopped moving',
                        'converged: no step lowers the objective any more', 'no sensitivity left')[
                            max(0, min(3, int(lib.libfive_tetflow_stat(model_flow._handle.ptr, 24))))]

    def shape(self, iteration=None):
        ''' The body: where the level set is positive (its boundary the zero level, a fraction of an element
            exact); iteration=k the body after iteration k (0 the body as given) '''
        d = self.level if iteration is None else self.levels[iteration]
        # (inside the fluid domain: the level set is a field on its mesh, continued outside it by its nearest value)
        out = (-d).max(self.domain)
        if self.region is not None:
            out = out.max(self.region)
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
                label = 'the body as given' if k == 0 else 'iteration %d' % k
                last = k == n - 1
                step = {'label': label}
                if last:
                    # (the final body: the real flow around it)
                    step['channels'] = [(name, _LABELS[name], getattr(real, name)) + tuple(real._ranges[name]) for name in _FIELDS]
                    step['lines'] = real.streamlines()
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
                     'fluid_steps': fluid_steps, 'body_steps': body_steps, 'real': real}
            self._shown = shown
        real = shown['real']
        fluid = colored(shown['fluid'], real.speed, range=real._ranges['speed'], label=_LABELS['speed'], colormap='turbo')
        fluid._color_fields = shown['_color_fields']
        fluid._color_field_name = 'speed'
        fluid._flow_lines = shown['_flow_lines']
        fluid._flow_range = shown['_flow_range']
        fluid._color_detail = float(self.element_size or 0)
        body = Shape(lib.libfive_tree_copy(shown['body'].ptr))
        body._bounds = self.bounds
        if len(shown['fluid_steps']) > 1:
            fluid._color_steps = shown['fluid_steps']
            fluid._color_step = len(shown['fluid_steps']) - 1
            body._color_steps = shown['body_steps']
            body._color_step = len(shown['body_steps']) - 1
        return [fluid, body]

    def __repr__(self):
        d, l = self.drag, self.lift
        s = ('Flow topology optimization: drag {:.4g} -> {:.4g} N, lift {:.4g} -> {:.4g} N in the optimiser\'s model '
             '({} iterations{}, {}; {:.1f} s)').format(
                 d[0] if d else 0, d[-1] if d else 0, l[0] if l else 0, l[-1] if l else 0, self.iterations,
                 ', %d steps taken back' % self.steps_back if self.steps_back else '', self.stopped, self.seconds)
        if self.real_drag is not None:
            s += '\n  the real flow around the final body: drag {:.4g} N, lift {:.4g} N'.format(self.real_drag, self.real_lift)
        return s


_shape_cache = OrderedDict()


def flow_topology_optimization(body, domain, conditions, fluid=water, objective='drag', volume=1.0, region=None, keep=None,
                            avoid=None, element_size=None, iterations=40, filter_radius=None, move=0.5, darcy=0.1,
                            extrude=None, flow_direction=None, lift_direction=None, bounds=None, cache=True):
    ''' Shape optimisation of a body in a flow: `body` (a Shape, the solid) sits in `domain` (the fluid domain
        it is in, a Shape that holds the body's place too) with the flow's `conditions` (inlet(...),
        outlet(...), slip(...), wall(...) as for fluid_analysis); the optimiser changes the body's shape, and
        topology, to make it best in the stream.

        objective   'drag' (the least force along the flow), 'lift' (the most force across it), or
                    (w_drag, w_lift): w_drag * drag - w_lift * lift is minimised
        volume      what the body may use of its own volume: a number keeps it (1.0: the same volume), a pair
                    (least, most) bounds it (0.5, 1.5); None for no bound on that side
        region      where material may be (a Shape; default: anywhere in the domain); the body only shrinks,
                    grows and moves inside it
        keep        regions that stay solid (a Shape, or a list) -- a mounting, a shaft
        avoid       regions that stay fluid
        element_size  mm (default 40 elements along the longest side); the flow is solved once per iteration,
                    with its adjoint, so it costs about two flow analyses per iteration
        iterations  at most (it stops when the design stops moving)
        filter_radius  the level set's smoothing radius, mm (default 1.5 elements): the smallest feature
        move        the most the boundary moves in one iteration, in elements (0.5 by default; a step that
                    raises the objective is taken back and halved)
        darcy       the solid's permeability relative to the element: its friction is mu / (darcy h^2), the
                    flow penetrates it by about sqrt(darcy) elements (0.1 by default: a third of an element)
        extrude     'x', 'y' or 'z': the body is the same all along that axis (a 2D shape through a slab)
        flow_direction, lift_direction  (dx, dy, dz): the drag and lift directions (default: the inlets' mean
                    direction, and perpendicular to it in the plane of the domain's two long axes)

        The body is a level set at the mesh's nodes (a smooth field whose zero level is the boundary, so
        the body's edge is placed to a fraction of an element and stays smooth); each element's share of
        the body is the exact fraction of it inside that boundary and sets its friction in the
        Navier-Stokes flow (Borrvall & Petersson's penalised solid); the force on the body is the momentum
        the flow loses in it; the sensitivities are the exact discrete adjoint's; the boundary moves down
        them, and the volume is held by offsetting the whole level.  The boundary splits and merges as it
        moves (the topology changes that way); a hole does not open in the middle of solid.  Returns a
        FlowTopologyResult (the body, the drag and lift per iteration, the flow around it).  An unchanged
        problem is cached. '''
    if getattr(lib, 'libfive_tetflow_optimize', None) is None:
        raise FeaError('this FielDes library is too old for flow shape optimization')
    if not isinstance(body, Shape) or not isinstance(domain, Shape):
        raise TypeError('flow_topology_optimization: the body and the domain must be Shapes')
    if region is not None and not isinstance(region, Shape):
        raise TypeError('flow_topology_optimization: region must be a Shape')
    if objective == 'drag':
        weights = (1.0, 0.0)
    elif objective == 'lift':
        weights = (0.0, 1.0)
    else:
        try:
            weights = (float(objective[0]), float(objective[1]))
        except (TypeError, IndexError, ValueError):
            raise ValueError("flow_topology_optimization: objective is 'drag', 'lift' or (w_drag, w_lift)")
    if isinstance(volume, (int, float)):
        vmin = vmax = float(volume)
    else:
        try:
            vmin = 0.0 if volume[0] is None else float(volume[0])
            vmax = 1e6 if volume[1] is None else float(volume[1])
        except (TypeError, IndexError, ValueError):
            raise ValueError('flow_topology_optimization: volume is a number or a pair (least, most), of the body\'s volume')
    if not (0 <= vmin <= vmax):
        raise ValueError('flow_topology_optimization: the least volume must be no more than the most, both at least 0')
    keep = [] if keep is None else list(keep if isinstance(keep, (list, tuple)) else [keep])
    avoid = [] if avoid is None else list(avoid if isinstance(avoid, (list, tuple)) else [avoid])
    # (what the body has excluded stays as it is: its locked fields are regions to keep)
    keep = keep + keep_regions(body)
    axes = {None: -1, 'x': 0, 'y': 1, 'z': 2}
    if extrude not in axes:
        raise ValueError("extrude is None, 'x', 'y' or 'z'")
    fd = tuple(float(v) for v in flow_direction) if flow_direction is not None else (0.0, 0.0, 0.0)
    ld = tuple(float(v) for v in lift_direction) if lift_direction is not None else (0.0, 0.0, 0.0)
    if len(fd) != 3 or len(ld) != 3:
        raise ValueError('flow_topology_optimization: the directions are (dx, dy, dz)')
    ekey = problem_key('flow_topology', body=body, domain=domain, part_bounds=getattr(domain, '_bounds', None),
                       conditions=conditions, fluid=fluid, weights=weights, volume=(vmin, vmax), region=region,
                       keep=keep, avoid=avoid, element_size=element_size, iterations=int(iterations),
                       filter_radius=filter_radius, move=float(move), darcy=float(darcy), extrude=extrude,
                       directions=(fd, ld), bounds=bounds) if cache else None
    if ekey is not None and ekey in _shape_cache:
        _shape_cache.move_to_end(ekey)
        cached = _shape_cache[ekey]
        if cached.body is body and cached.domain is domain:
            return cached
        r = object.__new__(FlowTopologyResult)
        r.__dict__.update(cached.__dict__)
        r.body, r.domain = body, domain
        return r
    handle, element_size, (lo, hi), items = _problem(domain, conditions, fluid, element_size, bounds, None, False, 60, 1.0,
                                                     'flow_topology_optimization')
    settings = {'weights': weights, 'volume': (vmin, vmax), 'filter_radius': float(filter_radius or 0.0),
                'iterations': int(iterations), 'move': float(move), 'extrude': axes[extrude], 'darcy': float(darcy)}

    def finish(handle, ptr, seconds):
        ''' The result from a solved problem (just optimised, or read back from its file) '''
        level = Shape(lib.libfive_tetflow_level(ptr))
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
        fdir = (d3[0], d3[1], d3[2])
        lib.libfive_tetflow_direction(ptr, 1, d3)
        ldir = (d3[0], d3[1], d3[2])
        flow = FluidResult(handle, domain, fluid, items)
        flow._bounds = (lo, hi)
        flow.element_size = element_size
        flow._read_steps()
        result = FlowTopologyResult(flow, body, domain, region, level, levels, drag, lift, (fdir, ldir), settings,
                                    element_size, (lo, hi), seconds)
        # the real flow around the final body: the body a wall at rest, the same conditions (cached like any flow)
        result.flow = fluid_analysis(result.fluid_shape(), items, fluid=fluid, element_size=element_size, bounds=(lo, hi),
                                     cache=cache)
        wf = result.flow.wall_force
        result.real_drag = sum(wf[i] * fdir[i] for i in range(3))
        result.real_lift = sum(wf[i] * ldir[i] for i in range(3))
        return result

    # solved in an earlier session: read back (see result_cache.py)
    loaded = result_cache.load('tetflow', ekey, 'flow topology optimization')
    if loaded is not None:
        result = finish(_Handle(loaded[0]), loaded[0], float(loaded[1].get('seconds', 0)))
        _shape_cache[ekey] = result
        return result
    ptr = handle.ptr
    if not lib.libfive_tetflow_prepare(ptr):
        raise FeaError('flow_topology_optimization: ' + lib.libfive_tetflow_message(ptr).decode('utf-8', 'replace'))
    keep_arr = (ctypes.c_void_p * max(1, len(keep)))(*[_shape(r, 'keep').ptr for r in keep])
    avoid_arr = (ctypes.c_void_p * max(1, len(avoid)))(*[_shape(r, 'avoid').ptr for r in avoid])
    if ekey is not None:
        lib.libfive_tetflow_set_salt(ptr, result_cache.salt('tetflow', ekey))
    t0 = time.time()
    ok = lib.libfive_tetflow_optimize(ptr, body.ptr, region.ptr if region is not None else None, weights[0], weights[1],
                                      fd[0], fd[1], fd[2], ld[0], ld[1], ld[2], vmin, vmax, settings['filter_radius'],
                                      settings['iterations'], settings['move'], keep_arr, len(keep), avoid_arr, len(avoid),
                                      settings['extrude'], settings['darcy'])
    if not ok:
        raise FeaError('flow_topology_optimization: ' + lib.libfive_tetflow_message(ptr).decode('utf-8', 'replace'))
    result = finish(handle, ptr, time.time() - t0)
    result_cache.save('tetflow', ekey, ptr, 'flow topology optimization', {'seconds': result.seconds})
    if ekey is not None:
        _shape_cache[ekey] = result
        while len(_shape_cache) > 8:
            _shape_cache.popitem(last=False)
    return result
