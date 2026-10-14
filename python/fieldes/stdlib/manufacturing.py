'''
Manufacturing: constraints a part has to keep to be made, a check of them, and a shell that changes the part until it keeps them.

    from fieldes import *

    printer = fff_constraint(nozzle=0.4)                         # what a printer with a 0.4 mm nozzle can make
    report = manufacturing_analysis(part, constraints=[printer])   # where the part breaks them (coloured on the part), and how much
    better = manufacturing_optimization(part, constraints=[printer])   # the part, changed until it keeps them, and what was changed

A CONSTRAINT is an input, like a support or a load (the Engineering menu > Manufacturing makes them).  `manufacturing_constraint` has
every rule, each optional -- only what is written is asked -- and `fff_constraint`, `cnc_constraint` and `molding_constraint` are
presets of it with the numbers of a process:

    overhang=45        the steepest a surface that faces down may be, in degrees from the vertical, without a support under it (off unless
                       asked: supports are normal in printing; it is for what supports cannot reach -- channels, bores, cosmetic faces,
                       lattices -- or a printer without them: scope it with region=)
    min_wall=0.8       the thinnest wall (mm)
    min_gap=0.4        the narrowest gap between walls, a hole or a slot (mm)
    max_bridge=20      the longest flat span with nothing under it (mm)
    min_radius=3       the smallest radius of an inside corner (mm): a milling cutter's radius
    draft=1.0          the least slope of a wall away from the pull direction (degrees: a moulded part has to come out)
    undercut_along=(0,0,1)   no feature that a tool or a mould half coming along this direction (either way) cannot reach
    build_direction    the way up of a print (default z); None leaves it to be found, the way that needs the least
    region=            a shape: the constraint holds only inside it (a part can have a constraint for each of its regions)

The CHECK works on the part's own field and nothing else: the normals of the part by central differences, and what a ball of the
size of the rule can reach from each point of the surface (a wall is thin where a ball of half its thickness does not fit inside
it, a gap or an inside corner is too narrow where one does not fit in the air), by shifted copies of the field -- every rule is a
FIELD on the part, drawn as colours on it, and nothing is meshed.  The numbers of the report (how much of the surface breaks a
rule, where) are read on a few thousand points of the surface, found by projecting random points onto it: they are measurements,
never drawn and never used to build anything.

The SHELL changes the field with other means -- each remedy is a field made of shifted copies of the part's own: a gusset that
makes an overhang stand at the angle asked (the body under it grown at that slope), a teardrop cut for a channel, an offset that
follows how thin a wall is, a fill of the gap that is too narrow, a rounding of the inside corners (the part closed by a ball), a
draft, a fill of an undercut.  They are all made from the part as it is -- shifted copies of shifted copies would multiply -- and
united, what they add and then what they cut; what each changed is measured, and one that would change more than `max_change`
of the part is not made.  Then the check is made again on what it made, and what it could not fix is said, with where.  A region
that is excluded (exclude) or kept is never touched.  Give the result to the shell again for a second pass.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import os
import random
import sys
import time
from collections import OrderedDict

from fieldes.i18n import tr as _tr
from fieldes.shape import Shape
from fieldes.stdlib.fea import FieldResult, FeaError, colored, _register_slots, _given, _use_conditions, _with_conditions, _united
from fieldes.stdlib.fields import _shape_bounds
from fieldes.stdlib.excluded import keep_regions, locks_of

__all__ = ['manufacturing_constraint', 'fff_constraint', 'cnc_constraint', 'molding_constraint',
           'manufacturing_analysis', 'manufacturing_optimization', 'ManufacturingConstraint', 'ManufacturingAnalysis', 'ManufacturingResult']


def _progress(fraction, text):
    ''' What the bar of the run says (fraction 0 to 1, and what is being done) '''
    try:
        from fieldes import run_progress
        run_progress.report(fraction, text)
    except Exception:
        pass


def _log(started, text):
    ''' A line of what the shell is doing and how long it took, to FielDes's log, when FIELDES_MFG_TIMING is set '''
    if os.environ.get('FIELDES_MFG_TIMING'):
        out = sys.stderr or sys.__stderr__
        if out is not None:
            out.write('[manufacturing] %7.2f s  %s' % (time.time() - started, text) + chr(10))
            out.flush()


class ManufacturingError(FeaError):
    ''' A manufacturing check or remedy cannot be made as asked '''


# ---------------------------------------------------------------------------
# Constraints

RULES = ('overhang', 'min_wall', 'min_gap', 'max_bridge', 'min_radius', 'draft', 'undercut_along')

# What each rule says, with its unit: (words, unit of what it is measured in)
_RULE_TEXT = OrderedDict([
    ('overhang', (_tr('overhang'), '°')),
    ('min_wall', (_tr('thin wall'), 'mm')),
    ('min_gap', (_tr('narrow gap'), 'mm')),
    ('max_bridge', (_tr('long bridge'), 'mm')),
    ('min_radius', (_tr('sharp inside corner'), 'mm')),
    ('draft', (_tr('too little draft'), '°')),
    ('undercut_along', (_tr('undercut'), '')),
])

# What is measured of a rule is allowed to be this much short of its limit before it counts (a share of the limit): what the part's field and
# a few probes of it can tell is not exact, and a wall that is 0.78 thick for 0.8 asked is not what the check is for
GRACE = 0.05


def _unit(v, what):
    try:
        x, y, z = (float(c) for c in v)
    except (TypeError, ValueError):
        raise ManufacturingError('%s is a direction: three numbers, like (0, 0, 1)' % what) from None
    n = math.sqrt(x * x + y * y + z * z)
    if not n > 0:
        raise ManufacturingError('%s: a direction cannot be (0, 0, 0)' % what)
    return (x / n, y / n, z / n)


def _positive(v, what):
    try:
        x = float(v)
    except (TypeError, ValueError):
        raise ManufacturingError('%s is a number (mm)' % what) from None
    if not x > 0:
        raise ManufacturingError('%s must be more than 0' % what)
    return x


class ManufacturingConstraint:
    ''' What a part has to keep to be made: a set of rules -- `overhang`, `min_wall`, `min_gap`, `max_bridge`, `min_radius`, `draft`, `undercut_along`,
        each optional -- with the directions they are about and, if given, the region they hold in.  Made by `manufacturing_constraint` and by
        the presets `fff_constraint`, `cnc_constraint` and `molding_constraint`; it is given to `manufacturing_analysis` and
        `manufacturing_optimization` in `constraints=`. '''

    _call = 'manufacturing_constraint(...)'
    _no_gizmo = True

    def __init__(self, name, rules, build_direction, pull_direction, region, overhang_fix, gap_fix, support=None, support_gap=0.3, no_support=()):
        self.name = name
        self.support = support
        self.support_gap = support_gap
        self.no_support = list(no_support)
        self.rules = OrderedDict((k, rules[k]) for k in RULES if rules.get(k) is not None)
        self.build_direction = build_direction
        self.pull_direction = pull_direction
        self.region = region
        self.overhang_fix = overhang_fix
        self.gap_fix = gap_fix

    @property
    def free_direction(self):
        ''' Whether the way up of the print is left to be found '''
        return self.build_direction is None and 'overhang' in self.rules

    def __repr__(self):
        parts = []
        for k, v in self.rules.items():
            if k == 'undercut_along':
                parts.append('undercut_along=(%.3g, %.3g, %.3g)' % tuple(v))
            else:
                parts.append('%s=%.4g' % (k, v))
        where = ' in a region' if self.region is not None else ''
        return '%s(%s)%s' % (self.name, ', '.join(parts) or 'no rule', where)

    def _display(self):
        return None


def manufacturing_constraint(overhang=None, min_wall=None, min_gap=None, max_bridge=None, min_radius=None, draft=None, undercut_along=None,
                             build_direction=(0, 0, 1), pull_direction=(0, 0, 1), region=None, overhang_fix='fill', gap_fix='fill',
                             support=None, support_gap=0.3, no_support=None, name='manufacturing_constraint'):
    ''' A constraint a part has to keep to be made, every rule optional (only what is written is asked):

            overhang      the steepest a surface that faces down may be without a support under it, in degrees from the vertical (45: a
                          surface that leans out more than 45 degrees needs one).  Off unless given
            min_wall      the thinnest wall, mm
            min_gap       the narrowest gap between two walls, a hole or a slot, mm
            max_bridge    the longest flat span with nothing under it, mm
            min_radius    the smallest radius of an inside corner, mm (a milling cutter cannot cut a sharper one than its own radius)
            draft         the least slope of a wall away from `pull_direction`, degrees (a moulded part has to come out of its mould)
            undercut_along  a direction: nothing that a tool or a mould half coming along it, from either side, cannot reach
            build_direction  the way up of a print, a direction (default (0, 0, 1)); None leaves it to be found, the way up that needs the least
            pull_direction   the way a mould opens, for `draft` (default (0, 0, 1))
            region        a shape: the constraint holds only inside it (default: everywhere).  A part can be given a constraint for each region
            overhang_fix  what the shell does to an overhang: 'fill' grows the body under it at the angle asked (a gusset), 'cut' cuts the
                          air above it into a teardrop (for a channel or a bore); a region of the other kind is a constraint of its own
            gap_fix       what the shell does to a gap that is too narrow: 'fill' closes it, 'widen' makes it as wide as asked
            support       the kind of support the shell may add under an overhang it cannot fix by changing the part: 'block' (a block straight under the overhang, down to the plate or
                          the part below); None (the default) makes none -- an overhang the part's own changes do not fix is reported.  Supports are a separate body of the result
                          (`result.supports`), exported as an object of their own
            support_gap   the clearance between a support and the part, mm (default 0.3: what lets it be broken off)
            no_support    a shape, or a list of them: where no support may be made (kept clear for the surface that has to be clean, or a slicer's own supports)

        The presets `fff_constraint`, `cnc_constraint` and `molding_constraint` write the numbers of a process.  Several constraints can be
        given together: a rule is kept when it is kept by each of them. '''
    rules = {}
    if overhang is not None:
        a = float(overhang)
        if not 0.0 <= a < 90.0:
            raise ManufacturingError('overhang is an angle in degrees from the vertical, from 0 up to but not 90: a surface that leans out more '
                                     'than that needs a support (45 is the usual)')
        rules['overhang'] = a
    for key, value in (('min_wall', min_wall), ('min_gap', min_gap), ('max_bridge', max_bridge), ('min_radius', min_radius)):
        if value is not None:
            rules[key] = _positive(value, key)
    if draft is not None:
        d = float(draft)
        if not 0.0 < d < 45.0:
            raise ManufacturingError('draft is an angle in degrees, more than 0 and less than 45 (1 to 2 is usual for a moulded part)')
        rules['draft'] = d
    if undercut_along is not None:
        rules['undercut_along'] = _unit(undercut_along, 'undercut_along')
    if support not in (None, 'block'):
        raise ManufacturingError("support is the kind of support that may be added under an overhang that cannot be fixed: 'block' (more kinds are not made yet), or None")
    if support is not None and overhang is None:
        raise ManufacturingError('support is for an overhang: give the overhang rule too (overhang=45)')
    support_gap = float(support_gap)
    if not support_gap >= 0:
        raise ManufacturingError('support_gap is the clearance between a support and the part, in mm: 0 or more')
    blockers = [Shape.wrap(r) for r in (list(no_support) if isinstance(no_support, (list, tuple)) else ([no_support] if no_support is not None else []))]
    if overhang_fix not in ('fill', 'cut'):
        raise ManufacturingError("overhang_fix is 'fill' (grow the body under the overhang) or 'cut' (cut the air above it into a teardrop)")
    if gap_fix not in ('fill', 'widen'):
        raise ManufacturingError("gap_fix is 'fill' (close the gap) or 'widen' (make it as wide as asked)")
    if region is not None and not isinstance(region, Shape):
        region = Shape.wrap(region)
    return ManufacturingConstraint(name, rules, None if build_direction is None else _unit(build_direction, 'build_direction'),
                                   _unit(pull_direction, 'pull_direction'), region, overhang_fix, gap_fix, support, support_gap, blockers)


def fff_constraint(nozzle=0.4, build_direction=(0, 0, 1), overhang=None, max_bridge=20.0, region=None):
    ''' What a fused-filament (FFF) printer can make, for a nozzle of `nozzle` mm.  min_wall is two nozzle widths (the least that prints as a
        wall: published guides give 0.8 to 1.2 mm for 0.4 mm, 1.2 and more for strength), min_gap one nozzle width (a narrower gap
        prints closed), max_bridge 20 mm (guides give up to about 30 mm for a clean bridge, less with a soft material).  The overhang rule is off:
        a slicer puts supports where it is needed -- give overhang=45 to ask for none (a printer without supports, or a region they cannot reach
        -- scope it with region=).  Guides give 45 degrees as the limit; it depends on the material and the printer: calibrate with a test print. '''
    n = _positive(nozzle, 'nozzle')
    return manufacturing_constraint(overhang=overhang, min_wall=2.0 * n, min_gap=n, max_bridge=max_bridge, build_direction=build_direction,
                                    region=region, name='fff_constraint')


def cnc_constraint(tool_diameter=6.0, axis=(0, 0, 1), min_wall=0.8, region=None):
    ''' What a 3-axis milling machine can make with a cutter of `tool_diameter` mm coming along `axis`: an inside corner cannot be sharper than
        the cutter's radius (min_radius) and a slot cannot be narrower than the cutter (min_gap), no feature can be out of reach of the cutter
        from the top and from the bottom (undercut_along), and a wall of metal is not thinner than min_wall (0.8 mm for metal, 1.5 or more for
        plastic: thin walls chatter).  Guides advise a radius a little more than the tool's, and a corner radius of a third of the
        depth of the pocket. '''
    d = _positive(tool_diameter, 'tool_diameter')
    return manufacturing_constraint(min_wall=min_wall, min_gap=d, min_radius=d / 2.0, undercut_along=axis, region=region, name='cnc_constraint')


def molding_constraint(min_wall=1.0, draft=1.0, pull_direction=(0, 0, 1), region=None):
    ''' What an injection-moulded part needs: walls not thinner than min_wall (1 to 4 mm by the plastic, uniform), a draft of `draft` degrees
        (1 is the baseline, 2 and more for a texture or a tall wall) on every wall, no undercut along the way the mould opens, and inside corners
        of half the wall's thickness (the usual interior fillet).  Published guides, not a standard: ask your plastic's maker. '''
    w = _positive(min_wall, 'min_wall')
    return manufacturing_constraint(min_wall=w, draft=draft, undercut_along=pull_direction, min_radius=0.5 * w, pull_direction=pull_direction,
                                    region=region, name='molding_constraint')


for _f in (manufacturing_constraint, fff_constraint, cnc_constraint, molding_constraint):
    _f._required = ()

_register_slots({'constraints': ((ManufacturingConstraint,), 'manufacturing_constraint(...), fff_constraint(...), cnc_constraint(...) and molding_constraint(...) items')})


# ---------------------------------------------------------------------------
# Fields made of shifted copies of the part's own field

def _xyz():
    return Shape.X(), Shape.Y(), Shape.Z()


def _at(f, dx, dy, dz):
    ''' f(p + (dx, dy, dz)): the field moved by the vector the other way (numbers or fields) '''
    x, y, z = _xyz()
    return f.remap(x + dx, y + dy, z + dz)


def _circle(k, phase=0.0):
    ''' k unit vectors in a circle about z, the first along x (an axis-aligned feature has one of them straight at it) '''
    return [(math.cos(phase + 2.0 * math.pi * i / k), math.sin(phase + 2.0 * math.pi * i / k)) for i in range(k)]


def _frame(d):
    ''' Two unit vectors that with d make a right-handed frame (e1, e2, d) '''
    ref = (1.0, 0.0, 0.0) if abs(d[0]) < 0.9 else (0.0, 1.0, 0.0)
    e1 = (d[1] * ref[2] - d[2] * ref[1], d[2] * ref[0] - d[0] * ref[2], d[0] * ref[1] - d[1] * ref[0])
    n = math.sqrt(sum(c * c for c in e1))
    e1 = tuple(c / n for c in e1)
    e2 = (d[1] * e1[2] - d[2] * e1[1], d[2] * e1[0] - d[0] * e1[2], d[0] * e1[1] - d[1] * e1[0])
    return e1, e2


def _sphere(k):
    ''' k directions spread evenly over the sphere (a Fibonacci spiral) '''
    out = []
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(k):
        z = 1.0 - 2.0 * (i + 0.5) / k
        r = math.sqrt(max(0.0, 1.0 - z * z))
        out.append((r * math.cos(golden * i), r * math.sin(golden * i), z))
    return out


class _Normals:
    ''' The gradient of a field by central differences, and its unit vector, as fields: what faces which way, at every point '''

    def __init__(self, f, step):
        self.step = step
        h = step
        gx = (_at(f, h, 0, 0) - _at(f, -h, 0, 0)) * (0.5 / h)
        gy = (_at(f, 0, h, 0) - _at(f, 0, -h, 0)) * (0.5 / h)
        gz = (_at(f, 0, 0, h) - _at(f, 0, 0, -h)) * (0.5 / h)
        self.magnitude = (gx * gx + gy * gy + gz * gz).sqrt()
        # (on the middle of a gap or a wall the slope is 0 and there is no normal: a fixed direction stands in for it there, and nowhere the slope is a few thousandths or more)
        bias = 1.0 - (self.magnitude * 1000.0).min(1)
        gx, gy, gz = gx + bias * 0.57735, gy + bias * 0.57735, gz + bias * 0.57735
        m = (gx * gx + gy * gy + gz * gz).sqrt().max(1e-6)
        self.x, self.y, self.z = gx / m, gy / m, gz / m
        self.scale = self.magnitude.max(0.25).min(4.0)        # (what a field's value is divided by to be a distance, as far as it is told by its slope)


def _dot(n, d):
    return n.x * d[0] + n.y * d[1] + n.z * d[2]


def _join(op, values, tie):
    ''' The largest ('max') or the smallest ('min') of the fields `values`, each moved by `tie` more than the one before (a few microns).  Looking at a part from where the
        lookup does not move what it sees -- straight up at a vertical wall, along a plane -- gives exactly the same value at every lookup over a whole region, and a
        min or max of equal values is a tie that the mesher, when it places the vertices of the surface, settles by trying every way it could go: 24 such lookups took
        53 s to mesh, and 0.4 s when they were a few microns apart.  The microns are not in the shape: they are far below anything a rule is measured in. '''
    out = None
    for i, v in enumerate(values):
        w = v + tie * i if i else v
        out = w if out is None else (out.max(w) if op == 'max' else out.min(w))
    return out


def _near(delta, f, width, top):
    ''' `delta` (a field between 0 and `top`) where the part's surface is within `width`, and 0 beyond: a remedy that changes the surface only near it is limited by how far a point
        is from it, so that the mesher can tell a cell far from the surface at once, without working through the remedy in it.  The field is the one it was where it matters '''
    return delta.min((width - f.abs()) * (10.0 * top / width)).max(0)


def _within_reach(f, d, sign, reach, tie, count=16):
    ''' Above 0 where the part (where `f` is 0 or less) is not met within `reach` along `sign` times `d`, and 0 or less where it may be: the part found at `count` places along the
        line, less the most the field can change between two of them.  What a remedy that only matters where there is part that way is limited by, so that the mesher can
        tell a cell where there is none at once '''
    step = reach / float(count)
    values = [_at(f, sign * d[0] * step * j, sign * d[1] * step * j, sign * d[2] * step * j) for j in range(1, count + 1)]
    return _join('min', values, tie) - step


_BIG = 1.0e3
# Of two limits of the same rule, the one that asks more
_STRICTEST = {'overhang': min, 'max_bridge': min, 'min_wall': max, 'min_gap': max, 'min_radius': max, 'draft': max, 'undercut_along': lambda a, b: a}


class _Context:
    ''' Everything the fields of the rules are made from for one part: its field, how big it is, its normals '''

    def __init__(self, part, bounds=None):
        self.field = Shape.wrap(part)
        lo, hi = _shape_bounds(self.field, bounds)
        self.lo, self.hi = tuple(float(c) for c in lo), tuple(float(c) for c in hi)
        self.size = max(self.hi[i] - self.lo[i] for i in range(3)) or 1.0
        self.step = max(1e-3 * self.size, 0.004)
        self.tie = max(5e-7 * self.size, 2e-6)         # (how far apart the lookups that are combined are kept in value: see _join)
        self.plate_box = (self.lo, self.hi)           # (where the print plate is, from the part: a check of what was made keeps the part's, not the made part's padded box)
        self._normals = None

    @property
    def normals(self):
        if self._normals is None:
            self._normals = _Normals(self.field, self.step)
        return self._normals


def _scoped(margin, constraint):
    ''' A rule holds only inside the constraint's region: elsewhere its margin is far below 0 '''
    if constraint.region is None:
        return margin
    return margin - _BIG * constraint.region.max(0)


def _reach(ctx, radius, sign):
    ''' What a ball of `radius` can reach from each point of the surface: sign -1 into the body (how deep a ball that touches the point can sit
        inside it), +1 into the air (how far from the part the centre of a ball that touches the point can be).  The most of the field,
        times the sign and over its slope, at probes along the normal and on the ball's sphere -- a number up to `radius` (the ball fits
        whole) and less where a wall or a gap or a corner is too narrow for it.  The point's own value is added to the probes' (the surface has 0). '''
    n = ctx.normals
    f = ctx.field
    probes = []
    for k in range(1, 7):                          # (along the normal: the middle of a wall, a gap, a corner)
        s = radius * k / 6.0
        probes.append(_at(f, sign * n.x * s, sign * n.y * s, sign * n.z * s) * sign)
    for u in _sphere(10):                          # (and the ball's own sphere: for a normal that is not true)
        probes.append(_at(f, radius * u[0], radius * u[1], radius * u[2]) * sign)
    best = probes[0]
    for p in probes[1:]:
        best = best.max(p)
    return best.max(0) / n.scale


def _chord(ctx, limit, inward):
    ''' How thick the part is along the normal at each point of its surface -- through the material (`inward`) or through the air -- as the distance
        from the surface to where the line along the normal meets the surface again, found at 14 places along the line (up to a tenth more than
        `limit`) and between them by where the field crosses 0: `limit` times 1.1 where it does not within that.  From the nearest point of the
        surface, so that it is the same for a point off it.  The line along a normal is not fooled by an edge, as a ball sitting in a corner is. '''
    n, f = ctx.normals, ctx.field
    sign = -1.0 if inward else 1.0
    count = 14
    reach_ = 1.1 * limit
    flip = 1.0 if inward else -1.0                  # (the field's value, positive once past the surface)
    previous = Shape.wrap(-1e-3 * limit)            # (just inside, at the surface)
    founds = [Shape.wrap(1.0e5)]
    for k in range(1, count + 1):
        s = reach_ * k / float(count)
        s0 = reach_ * (k - 1) / float(count)
        v = flip * _at(f, -(f + (-sign) * s) * n.x, -(f + (-sign) * s) * n.y, -(f + (-sign) * s) * n.z)
        a, b = previous, v
        crossing = ((a * (-1.0 / (1e-4 * limit))) + 0.5).max(0).min(1) * ((b * (1.0 / (1e-4 * limit))) + 0.5).max(0).min(1)
        frac = ((-a) / (b - a + 1e-9)).max(0).min(1)
        founds.append(s0 + (s - s0) * frac + 1.0e5 * (1.0 - crossing))
        previous = v
    return _join('min', founds, ctx.tie).min(reach_)


def _flatness(ctx, reach_):
    ''' 1 where the surface is flat within `reach_` of a point and 0 where it turns -- an edge, a corner, a tight curve: the normal at four points round it (`reach_`
        off along the surface) against its own, the least of the four, from 0.8 (0) to 0.9 (1) of a dot product.  The thickness along a normal means a wall's
        thickness where the surface is flat, and not on the rounded edge of a thin wall, where the normal looks across the corner '''
    n = ctx.normals
    x, y, z = _xyz()
    # (two directions along the surface, from a direction that is not often along a normal)
    ref = (0.30, 0.50, 0.81)
    cx = n.y * ref[2] - n.z * ref[1]
    cy = n.z * ref[0] - n.x * ref[2]
    cz = n.x * ref[1] - n.y * ref[0]
    cm = (cx * cx + cy * cy + cz * cz).sqrt().max(1e-3)
    t1 = (cx / cm, cy / cm, cz / cm)
    t2 = (n.y * t1[2] - n.z * t1[1], n.z * t1[0] - n.x * t1[2], n.x * t1[1] - n.y * t1[0])
    least = None
    for t in (t1, t2):
        for sign in (1.0, -1.0):
            dx, dy, dz = t[0] * (sign * reach_), t[1] * (sign * reach_), t[2] * (sign * reach_)
            nx, ny, nz = n.x.remap(x + dx, y + dy, z + dz), n.y.remap(x + dx, y + dy, z + dz), n.z.remap(x + dx, y + dy, z + dz)
            dot = n.x * nx + n.y * ny + n.z * nz
            least = dot if least is None else least.min(dot)
    return ((least - 0.8) * (1.0 / 0.1)).max(0).min(1)


def _rule_margins(ctx, constraints, direction, skip=()):
    ''' {rule: (margin field, limit, unit)}: for each rule that some constraint asks, a field that is above 0 where the part's surface breaks it (and by how
        much, in the unit of the rule), and the limit it is measured against (the strictest asked).  Several constraints asking the same rule: it is
        broken where it is broken for any of them. '''
    out = OrderedDict()
    n = ctx.normals
    f = ctx.field
    pi = math.pi
    for c in constraints:
        d = direction if c.build_direction is None else c.build_direction
        for rule, value in c.rules.items():
            if rule in skip:
                continue
            if rule == 'overhang':
                phi = (-_dot(n, d)).max(0).min(1).asin() * (180.0 / pi)        # (how far from vertical a face that looks down leans)
                height = (Shape.X() * d[0] + Shape.Y() * d[1] + Shape.Z() * d[2]) - (min(
                    (x * d[0] + y * d[1] + z * d[2]) for x in (ctx.plate_box[0][0], ctx.plate_box[1][0]) for y in (ctx.plate_box[0][1], ctx.plate_box[1][1])
                    for z in (ctx.plate_box[0][2], ctx.plate_box[1][2])))
                # (the first layer, on the plate, rests on it; and a normal read within a few steps of an edge or a crease is a mix of the two faces, not the lean of either)
                margin = phi - value - 1.0 - _BIG * (0.6 - height).max(0) - _BIG * (1.0 - _flatness(ctx, 3.0 * ctx.step))
            elif rule == 'min_wall':
                margin = (value - _chord(ctx, value, True)) - GRACE * value - _BIG * (1.0 - _flatness(ctx, 0.5 * value))
            elif rule == 'min_gap':
                margin = (value - _chord(ctx, value, False)) - GRACE * value - _BIG * (1.0 - _flatness(ctx, 0.5 * value))
            elif rule == 'min_radius':
                margin = (value - _reach(ctx, value, +1)) - GRACE * value
            elif rule == 'draft':
                dd = c.pull_direction
                delta = _dot(n, dd).abs().min(1).asin() * (180.0 / pi)
                margin = (value - delta) - GRACE * value
            elif rule == 'undercut_along':
                margin = _undercut_margin(ctx, value)
            elif rule == 'max_bridge':
                margin = _bridge_margin(ctx, value, d)
            else:
                continue
            margin = _scoped(margin, c)
            if rule in out:
                m, old, unit = out[rule]
                out[rule] = (m.max(margin), _STRICTEST[rule](old, value), unit)
            else:
                out[rule] = (margin, value, _RULE_TEXT[rule][1] or '')
    return out


def _samples_along(total, count):
    ''' Distances along a ray, close together near its start and wider apart further out: the first at a hundredth of the way, the last at the end '''
    return [total * ((k + 1.0) / count) ** 2 for k in range(count)]


def _undercut_margin(ctx, a):
    ''' 1 where a point of the surface can be reached from neither end of the direction `a`: a ray along it, from the point, meets the part both ways
        (above 0: undercut).  Minus 1 where it can.  The ray is read at 24 places along it, nearer ones closer together: a wall thinner than the space
        between them, far off, is not seen.  (Fields are read at fixed offsets, not walked: a field made of a walk of steps, each from the last, costs a
        multiple of itself at every step.  A face that looks along `a` is on the ray: it does not block it.) '''
    total = _extent_along(ctx, a)
    tol = 0.002 * ctx.size
    f = ctx.field
    blocked = []
    for sign in (1.0, -1.0):
        hit = None
        for s in _samples_along(total, 24):
            v = _at(f, a[0] * sign * s, a[1] * sign * s, a[2] * sign * s)
            b = (v * (-1.0 / tol)).max(0).min(1)
            hit = b if hit is None else hit.max(b)
        blocked.append(hit)
    return (blocked[0].min(blocked[1]) - 0.75) * 4.0


def _bridge_margin(ctx, limit, d):
    ''' How far past `limit` a flat roof is held at its two ends: the narrowest chord of the air under a surface that faces straight down, along eight
        directions across the way up, where a wall stops it at both ends (a ledge with an open end is an overhang, not a bridge).  The chord is read
        at 10 places along each ray from just under the roof, the first one inside the part being where the wall is. '''
    n = ctx.normals
    f = ctx.field
    eps = max(0.01 * ctx.size, 4 * ctx.step)
    e1, e2 = _frame(d)
    tol = 0.002 * ctx.size
    reach_ = 1.6 * (limit + ctx.size * 0.02)
    far = 1.0e5                                               # (where a ray does not meet the part: the chord is longer than anything)
    span = None
    for k in range(8):
        ang = math.pi * k / 8.0
        u = tuple(math.cos(ang) * e1[i] + math.sin(ang) * e2[i] for i in range(3))
        sides = []
        for sign in (1.0, -1.0):
            first = None
            for s in _samples_along(reach_, 10):
                v = _at(f, -d[0] * eps + u[0] * sign * s, -d[1] * eps + u[1] * sign * s, -d[2] * eps + u[2] * sign * s)
                where = s + far * (1.0 - (v * (-1.0 / tol)).max(0).min(1))       # (s where the ray is inside the part, far where it is not)
                first = where if first is None else first.min(where)
            sides.append(first)
        chord = sides[0] + sides[1]
        span = chord if span is None else span.min(chord)
    held = 1.0 - ((span - 0.5 * far) * (2.0 / far)).max(0).min(1)       # (1 where both ends are walls)
    flat = ((-_dot(n, d)) - 0.97).max(0) * (1.0 / 0.03)               # (a roof: it faces down within 14 degrees)
    cap = 10.0 * limit + ctx.size
    return (span.min(cap) - limit - GRACE * limit) - 3.0 * _BIG * (1.0 - held) - _BIG * (1.0 - flat.min(1))


# ---------------------------------------------------------------------------
# Measuring: a few thousand points of the surface, to report how much breaks a rule and where.  (A measurement: what is found here is
# said in numbers and is never drawn or built from)

def _values(field, points):
    ''' A field's value at points ((x, y, z) each), the library's batch evaluation '''
    from fieldes.stdlib.exporting import _eval_at
    if not points:
        return []
    return _eval_at(field, [c for p in points for c in p], len(points))


def _gradients(f, pts, h):
    ''' The value of the field at each point and its gradient by central differences, in one evaluation of all the points that takes (the field is
        set up for evaluation at every call, and that costs more than the points do) '''
    n = len(pts)
    flat = list(pts)
    for axis in range(3):
        for sign in (1.0, -1.0):
            for p in pts:
                q = list(p)
                q[axis] += sign * h
                flat.append(tuple(q))
    vals = _values(f, flat)
    out = []
    for i in range(n):
        g = tuple((vals[n * (1 + 2 * a) + i] - vals[n * (2 + 2 * a) + i]) / (2.0 * h) for a in range(3))
        out.append((vals[i], g))
    return out


def _surface_samples(ctx, count=5000, seed=7, rounds=6, batch_size=150000, seeds=None):
    ''' Points on the part's surface, the unit normal at each and the size of the field's gradient there: random points near the surface, each moved
        onto it by Newton steps along the gradient of the field (a surface found by evaluating the field: nothing is meshed).  Only where the part is
        on one side and air on the other (two faces that touch inside a part are a zero of the field the surface does not pass through).
        `seeds` are points to start from as well (the samples of the part as it was, for the surface of what was made of it: a field that is dear to
        evaluate is read at fewer points)
        [(point, normal, gradient size)] '''
    rng = random.Random(seed)
    f = ctx.field
    lo, hi, size, h = ctx.lo, ctx.hi, ctx.size, ctx.step
    pad = 0.03 * size
    band = max(0.04 * size, 4 * h)
    found = []
    for _ in range(rounds):                   # (a round is a few evaluations of the field, each costly to set up and cheap by the point: many points to a round)
        batch = [(rng.uniform(lo[0] - pad, hi[0] + pad), rng.uniform(lo[1] - pad, hi[1] + pad), rng.uniform(lo[2] - pad, hi[2] + pad))
                 for _ in range(batch_size)]
        near = [p for p, v in zip(batch, _values(f, batch)) if abs(v) < band]
        if seeds:
            near = near + [tuple(s) for s in seeds]
            seeds = None
        if not near:
            continue
        for _it in range(3):
            moved = []
            for p, (v, g) in zip(near, _gradients(f, near, h)):
                gg = g[0] * g[0] + g[1] * g[1] + g[2] * g[2]
                if gg < 1e-9:
                    moved.append(p)
                    continue
                k = v / gg
                moved.append((p[0] - k * g[0], p[1] - k * g[1], p[2] - k * g[2]))
            near = moved
        candidates = []
        for p, (v, g) in zip(near, _gradients(f, near, h)):
            if abs(v) > 0.002 * size + 2 * h:
                continue
            m = math.sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2])
            if m < 1e-6:
                continue
            candidates.append((p, (g[0] / m, g[1] / m, g[2] / m), m, v))
        reach_ = 2.0 * h
        probe = ([(p[0] + reach_ * n[0], p[1] + reach_ * n[1], p[2] + reach_ * n[2]) for p, n, _m, _v in candidates] +
                 [(p[0] - reach_ * n[0], p[1] - reach_ * n[1], p[2] - reach_ * n[2]) for p, n, _m, _v in candidates])
        vals = _values(f, probe)
        k = len(candidates)
        for item, vo, vi in zip(candidates, vals[:k], vals[k:]):
            if vo > 0 >= vi:
                found.append(item)
        if len(found) >= count:
            break
    return found[:count]


def _clusters(points, cell):
    ''' The groups the points fall into: neighbouring cells of a grid of `cell` joined.  [[point, ...], ...], the biggest first '''
    cells = {}
    for p in points:
        cells.setdefault((int(math.floor(p[0] / cell)), int(math.floor(p[1] / cell)), int(math.floor(p[2] / cell))), []).append(p)
    seen, groups = set(), []
    for start in cells:
        if start in seen:
            continue
        stack, group = [start], []
        seen.add(start)
        while stack:
            c = stack.pop()
            group.extend(cells[c])
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        n = (c[0] + dx, c[1] + dy, c[2] + dz)
                        if n in cells and n not in seen:
                            seen.add(n)
                            stack.append(n)
        groups.append(group)
    groups.sort(key=len, reverse=True)
    return groups


def _where(p):
    return '(%.4g, %.4g, %.4g)' % p


# ---------------------------------------------------------------------------
# The numbers of a report are worked out on the sample points themselves, by a second implementation of each rule -- plain arithmetic on
# the values of the part's field at points along the lines each rule looks along -- which does not build the (deeply nested) fields at all, and is
# a check of those fields: the colours on the part and the numbers in the report are made two ways

def _sub(pts, base, direction, s):
    return [(p[0] + s * direction[i][0], p[1] + s * direction[i][1], p[2] + s * direction[i][2]) for p, i in zip(pts, base)]


def _num_chord(f, pts, normals, offsets, limit, inward):
    ''' The thickness along the normal of each point (`_chord` as numbers): the distance from the surface to where the line along the normal meets
        it again, by where the field crosses 0 between 14 places, up to a tenth more than `limit`.  All the places of all the points are one
        evaluation of the field.  `offsets` are the field's values at the points (they are moved onto the surface first) '''
    count = 14
    reach_ = 1.1 * limit
    sign = -1.0 if inward else 1.0
    flip = 1.0 if inward else -1.0
    onto = [(p[0] - v * n[0], p[1] - v * n[1], p[2] - v * n[2]) for p, v, n in zip(pts, offsets, normals)]
    steps = [reach_ * k / float(count) for k in range(1, count + 1)]
    flat = []
    for s in steps:
        flat.extend((q[0] + sign * s * n[0], q[1] + sign * s * n[1], q[2] + sign * s * n[2]) for q, n in zip(onto, normals))
    vals = _values(f, flat)
    m = len(pts)
    eps = 1e-3 * limit
    out = []
    for i in range(m):
        previous, s_prev, chord = -eps, 0.0, reach_
        for k, s in enumerate(steps):
            b = flip * vals[k * m + i]
            if previous < 0 <= b:
                chord = s_prev + (s - s_prev) * (-previous) / (b - previous + 1e-9)
                break
            previous, s_prev = b, s
        out.append(min(chord, reach_))
    return out


def _num_reach(f, pts, normals, mags, radius, sign):
    ''' What a ball of `radius` can reach from each point (`_reach` as numbers), the probes of all the points one evaluation of the field '''
    flat = []
    for k in range(1, 7):
        s = radius * k / 6.0
        flat.extend((p[0] + sign * s * n[0], p[1] + sign * s * n[1], p[2] + sign * s * n[2]) for p, n in zip(pts, normals))
    for u in _sphere(10):
        flat.extend((p[0] + radius * u[0], p[1] + radius * u[1], p[2] + radius * u[2]) for p in pts)
    vals = _values(f, flat)
    m = len(pts)
    best = [0.0] * m
    for k in range(16):
        for i in range(m):
            v = sign * vals[k * m + i]
            if v > best[i]:
                best[i] = v
    return [b / min(4.0, max(0.25, g)) for b, g in zip(best, mags)]


def _num_undercut(f, pts, a, total, tol):
    ''' 1 where a ray along `a` from the point meets the part both ways, 0 where it does not: the 24 places along it of both ways, one evaluation '''
    m = len(pts)
    steps = _samples_along(total, 24)
    flat = []
    for sign in (1.0, -1.0):
        for s in steps:
            flat.extend((p[0] + sign * s * a[0], p[1] + sign * s * a[1], p[2] + sign * s * a[2]) for p in pts)
    vals = _values(f, flat)
    out = []
    for i in range(m):
        both = []
        for half in range(2):
            hit = 0.0
            for k in range(24):
                b = min(1.0, max(0.0, -vals[(half * 24 + k) * m + i] / tol))
                if b > hit:
                    hit = b
            both.append(hit)
        out.append(min(both))
    return out


def _num_flat(f, pts, normals, reach_, h):
    ''' `_flatness` as numbers: whether the surface is flat within `reach_` of each point (the least dot product of the normal at four points round it with its
        own is 0.9 or more).  One evaluation of the field '''
    ref = (0.30, 0.50, 0.81)
    around = []
    for p, n in zip(pts, normals):
        c = (n[1] * ref[2] - n[2] * ref[1], n[2] * ref[0] - n[0] * ref[2], n[0] * ref[1] - n[1] * ref[0])
        m = max(math.sqrt(c[0] ** 2 + c[1] ** 2 + c[2] ** 2), 1e-3)
        t1 = (c[0] / m, c[1] / m, c[2] / m)
        t2 = (n[1] * t1[2] - n[2] * t1[1], n[2] * t1[0] - n[0] * t1[2], n[0] * t1[1] - n[1] * t1[0])
        for t in (t1, t2):
            for sign in (1.0, -1.0):
                around.append((p[0] + sign * reach_ * t[0], p[1] + sign * reach_ * t[1], p[2] + sign * reach_ * t[2]))
    grads = _gradients(f, around, h)
    out = []
    for i, n in enumerate(normals):
        least = 1.0
        for j in range(4):
            g = grads[4 * i + j][1]
            m = math.sqrt(g[0] ** 2 + g[1] ** 2 + g[2] ** 2)
            if m < 1e-6:
                least = min(least, 0.0)
                continue
            least = min(least, (g[0] * n[0] + g[1] * n[1] + g[2] * n[2]) / m)
        out.append(least >= 0.9)
    return out


def _numeric_margins(ctx, constraints, direction, samples, skip=()):
    ''' {rule: [margin at each sample]} -- what `_rule_margins` gives, as numbers on the sample points '''
    pts = [s[0] for s in samples]
    normals = [s[1] for s in samples]
    mags = [s[2] for s in samples]
    offsets = [s[3] for s in samples]
    f = ctx.field
    out = OrderedDict()
    limits = OrderedDict()
    for c in constraints:
        d = direction if c.build_direction is None else c.build_direction
        for rule, value in c.rules.items():
            if rule in skip:
                continue
            if rule == 'overhang':
                plate = min(x * d[0] + y * d[1] + z * d[2] for x in (ctx.plate_box[0][0], ctx.plate_box[1][0]) for y in (ctx.plate_box[0][1], ctx.plate_box[1][1])
                            for z in (ctx.plate_box[0][2], ctx.plate_box[1][2]))
                m = []
                flat = _num_flat(f, pts, normals, 3.0 * ctx.step, ctx.step)
                for (p, nn), fl in zip(zip(pts, normals), flat):
                    phi = math.degrees(math.asin(max(0.0, min(1.0, -(nn[0] * d[0] + nn[1] * d[1] + nn[2] * d[2])))))
                    height = p[0] * d[0] + p[1] * d[1] + p[2] * d[2] - plate
                    m.append(phi - value - 1.0 - (_BIG * (0.6 - height) if height < 0.6 else 0.0) - (0.0 if fl else _BIG))
            elif rule == 'min_wall':
                flat = _num_flat(f, pts, normals, 0.5 * value, ctx.step)
                m = [(value - ch - GRACE * value) if fl else -_BIG for ch, fl in zip(_num_chord(f, pts, normals, offsets, value, True), flat)]
            elif rule == 'min_gap':
                flat = _num_flat(f, pts, normals, 0.5 * value, ctx.step)
                m = [(value - ch - GRACE * value) if fl else -_BIG for ch, fl in zip(_num_chord(f, pts, normals, offsets, value, False), flat)]
            elif rule == 'min_radius':
                m = [value - r - GRACE * value for r in _num_reach(f, pts, normals, mags, value, +1)]
            elif rule == 'draft':
                dd = c.pull_direction
                m = [value - math.degrees(math.asin(min(1.0, abs(nn[0] * dd[0] + nn[1] * dd[1] + nn[2] * dd[2])))) - GRACE * value for nn in normals]
            elif rule == 'undercut_along':
                blocked = _num_undercut(f, pts, value, _extent_along(ctx, value), 0.002 * ctx.size)
                m = [(b - 0.75) * 4.0 for b in blocked]
            elif rule == 'max_bridge':
                m = _num_bridge(ctx, pts, normals, value, d)
            else:
                continue
            if c.region is not None:
                reg = _values(c.region, pts)
                m = [x - _BIG * max(r, 0.0) for x, r in zip(m, reg)]
            if rule in out:
                out[rule] = [max(a, b) for a, b in zip(out[rule], m)]
                limits[rule] = (_STRICTEST[rule](limits[rule][0], value), limits[rule][1])
            else:
                out[rule] = m
                limits[rule] = (value, _RULE_TEXT[rule][1] or '')
    return out, limits


def _num_bridge(ctx, pts, normals, limit, d):
    ''' `_bridge_margin` as numbers, for the points that face down (roofs) only: the rest are far from being a bridge.  One evaluation of the field '''
    f = ctx.field
    eps = max(0.01 * ctx.size, 4 * ctx.step)
    e1, e2 = _frame(d)
    tol = 0.002 * ctx.size
    reach_ = 1.6 * (limit + ctx.size * 0.02)
    cap = 10.0 * limit + ctx.size
    roofs = [i for i, nn in enumerate(normals) if -(nn[0] * d[0] + nn[1] * d[1] + nn[2] * d[2]) > 0.97]
    out = [-_BIG] * len(pts)
    if not roofs:
        return out
    base = [(pts[i][0] - d[0] * eps, pts[i][1] - d[1] * eps, pts[i][2] - d[2] * eps) for i in roofs]
    steps = _samples_along(reach_, 10)
    dirs = []
    for k in range(8):
        ang = math.pi * k / 8.0
        dirs.append(tuple(math.cos(ang) * e1[i] + math.sin(ang) * e2[i] for i in range(3)))
    flat = []
    for u in dirs:
        for sign in (1.0, -1.0):
            for s in steps:
                flat.extend((q[0] + sign * s * u[0], q[1] + sign * s * u[1], q[2] + sign * s * u[2]) for q in base)
    vals = _values(f, flat)
    m = len(roofs)
    span = [1.0e9] * m
    block = 0
    for _u in dirs:
        ends = []
        for _sign in range(2):
            first = [1.0e9] * m
            for s in steps:
                for j in range(m):
                    if vals[block * m + j] < -tol and s < first[j]:
                        first[j] = s
                block += 1
            ends.append(first)
        for j in range(m):
            if ends[0][j] < 1.0e8 and ends[1][j] < 1.0e8:
                span[j] = min(span[j], ends[0][j] + ends[1][j])
    for j, i in enumerate(roofs):
        out[i] = (min(span[j], cap) - limit - GRACE * limit) if span[j] < 1.0e8 else -3.0 * _BIG
    return out


def _report_numeric(ctx, constraints, direction, samples, skip=()):
    ''' The report of `samples` for the rules of the constraints: {rule: {limit, unit, broken, share, worst, regions, samples}} '''
    margins, limits = _numeric_margins(ctx, constraints, direction, samples, skip)
    points = [s[0] for s in samples]
    n = len(points)
    report = OrderedDict()
    for rule, vals in margins.items():
        limit, unit = limits[rule]
        broken = [(p, v) for p, v in zip(points, vals) if v > 0]
        cell = max(limit if unit == 'mm' else 0.0, ctx.size / 25.0)
        regions = []
        for group in _clusters([p for p, _ in broken], cell)[:12]:
            xs, ys, zs = [p[0] for p in group], [p[1] for p in group], [p[2] for p in group]
            regions.append({'at': (sum(xs) / len(xs), sum(ys) / len(ys), sum(zs) / len(zs)), 'samples': len(group),
                            'share': len(group) / float(n) if n else 0.0, 'box': ((min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs)))})
        report[rule] = {'limit': limit, 'unit': unit, 'broken': len(broken), 'share': len(broken) / float(n) if n else 0.0,
                        'worst': max(vals) if vals else 0.0, 'regions': regions, 'samples': n}
    return report


class ManufacturingAnalysis(FieldResult):
    ''' The result of manufacturing_analysis(): where the part breaks the rules of its constraints.

            fields        {rule: field}: for each rule, a field that is above 0 on the surface where the rule is broken and by how much (in the
                          unit of the rule: mm, degrees), and `'all'`, the worst of them as a share of its limit -- what the part is coloured by;
                          each is also an attribute: `result.min_wall`
            report        {rule: {...}}: for each rule, `share` of the surface that breaks it, `worst`, the `regions` it is broken in
                          (`at`, the number of `samples` and the `box` of each) and its `limit`
            compliant     whether no rule is broken anywhere that was measured
            samples       how many points of the surface were measured (a measurement: the numbers of `report`)
            orientation   the way up the part was checked in, found when the constraint left it free

        Stated on its own the part is shown coloured by `all` (the card switches rule): blue where a rule is kept, red where it is broken.  It is a
        check of the part's own field, not a substitute for the check of the one who makes it. '''

    def __init__(self, part, constraints, fields, limits, report, samples, direction, seconds):
        self.part = part
        self.constraints = list(constraints)
        self._fields = fields
        self.limits = limits
        self.report = report
        self.samples = samples
        self.orientation = direction
        self.seconds = seconds
        self.compliant = all(r['broken'] == 0 for r in report.values())
        self._shown = None
        for rule, field in fields.items():
            if rule != 'all':
                setattr(self, rule, field)

    @property
    def fields(self):
        return OrderedDict(self._fields)

    def _as_field(self):
        return self._fields['all']

    def _display(self):
        if self._shown is None:
            out = colored(self.part, self._fields['all'], range=(0.0, 1.0), label=_tr('how far past its limit (share)'), colormap='turbo')
            out._color_fields = [('all', _tr('every rule: share of its limit'), self._fields['all'], 0.0, 1.0)]
            for rule, field in self._fields.items():
                if rule == 'all':
                    continue
                limit = self.limits[rule]
                out._color_fields.append((rule, '%s (%s)' % (_RULE_TEXT[rule][0], _RULE_TEXT[rule][1] or _tr('1 = broken')), field, 0.0,
                                          limit[0] if limit[1] else 1.0))
            out._color_field_name = 'all'
            rows = []
            for rule, r in self.report.items():
                rows.append((_RULE_TEXT[rule][0], '%.3g %%' % (100.0 * r['share']), ''))
            rows.insert(0, (_tr('every rule kept'), _tr('yes') if self.compliant else _tr('no'), ''))
            out._key_results = rows[:6]
            self._shown = out
        return self._shown

    def __repr__(self):
        lines = [_tr('Manufacturing analysis: %d point(s) of the surface measured (%.1f s)') % (self.samples, self.seconds)]
        lines.append(_tr('  every rule is kept') if self.compliant else _tr('  rules are broken:'))
        for rule, r in self.report.items():
            word, unit = _RULE_TEXT[rule]
            limit = ('(%.3g, %.3g, %.3g)' % tuple(r['limit'])) if rule == 'undercut_along' else ('%.4g %s' % (r['limit'], unit))
            if r['broken'] == 0:
                lines.append(_tr('    %s (%s): kept') % (word, limit))
                continue
            lines.append(_tr('    %s (%s): %.2g %% of the surface, worst %.3g %s, in %d place(s)') % (word, limit, 100.0 * r['share'], r['worst'], unit, len(r['regions'])))
            for g in r['regions'][:6]:
                lines.append('        %s  %.2g %%' % (_where(g['at']), 100.0 * g['share']))
        lines.append(_tr('  A check of the part\'s own field: not a substitute for the check of the one who makes it.'))
        return '\n'.join(lines)


def _all_field(margins):
    ''' The worst rule at each point, as a share of its limit, 0 where all are kept (what the part is coloured by) '''
    out = None
    for rule, (field, limit, unit) in margins.items():
        scale = 1.0 if rule == 'undercut_along' else limit
        share = (field * (1.0 / scale)).max(0).min(1.5)
        out = share if out is None else out.max(share)
    return out


def _constraints_given(constraints, what):
    items = _given(constraints, 'constraints', what)
    if not items:
        raise ManufacturingError('%s: give it at least one constraint -- constraints=[fff_constraint()], for instance' % what)
    return items


def _direction_of(constraints, ctx, samples):
    ''' The way up the part is checked in: the one the constraints name, or -- left free by a constraint with an overhang rule -- the one that
        leaves the least of the surface leaning out, by its samples (its normals; the first layer, resting on the plate, is not leaning out), and
        of those the one that makes the lowest print.  A way to stand the part found like this is a start, not a search for the best: the axes
        come first, then 40 directions spread over the sphere '''
    named = [c.build_direction for c in constraints if c.build_direction is not None]
    if named:
        return named[0]
    rules = [c.rules['overhang'] for c in constraints if 'overhang' in c.rules]
    if not rules:
        return (0.0, 0.0, 1.0)
    limit = min(rules)
    best, best_key = (0.0, 0.0, 1.0), None
    sinl = math.sin(math.radians(limit))
    corners = [(x, y, z) for x in (ctx.plate_box[0][0], ctx.plate_box[1][0]) for y in (ctx.plate_box[0][1], ctx.plate_box[1][1])
               for z in (ctx.plate_box[0][2], ctx.plate_box[1][2])]
    for d in [(0.0, 0.0, 1.0), (0.0, 0.0, -1.0), (1.0, 0.0, 0.0), (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, -1.0, 0.0)] + _sphere(40):
        first_layer = min(c[0] * d[0] + c[1] * d[1] + c[2] * d[2] for c in corners) + 0.6        # (as the rule has it: the first 0.6 mm rests on the plate)
        bad = sum(1 for p, n, _m, _v in samples if -(n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) > sinl and p[0] * d[0] + p[1] * d[1] + p[2] * d[2] > first_layer)
        key = (bad, round(_extent_along(ctx, d), 3))
        if best_key is None or key < best_key:
            best, best_key = d, key
    return best


@_with_conditions
def manufacturing_analysis(part, constraints, samples=5000):
    ''' Where a part breaks the rules of its constraints.  The part is checked on its own field, with other means than the shell that changes it
        (manufacturing_optimization): the normals, and what a ball of the size of each rule can reach from each point of the surface -- every
        rule a field on the part, drawn as colours on it, nothing meshed.  The numbers (how much of the surface breaks each rule, and where)
        are measured on `samples` points of the surface.

            part         a body
            constraints  manufacturing_constraint(...), fff_constraint(...), cnc_constraint(...), molding_constraint(...): one, or a list;
                         a rule is broken where it is broken for any of them
            samples      how many points of the surface are measured (default 5000)

        Returns a ManufacturingAnalysis: `.compliant`, `.report`, `.fields`.  It is a check of the part's own field and not a substitute for the check
        of the one who makes it. '''
    t0 = time.time()
    items = _constraints_given(constraints, 'manufacturing_analysis')
    _progress(0.05, _tr('measuring the surface of the part'))
    shape = Shape.wrap(part)
    ctx = _Context(shape)
    points = _surface_samples(ctx, int(samples))
    _progress(0.4, _tr('checking the rules'))
    direction = _direction_of(items, ctx, points)
    margins = _rule_margins(ctx, items, direction)
    if not margins:
        raise ManufacturingError('manufacturing_analysis: the constraints have no rule -- give one (min_wall=0.8, overhang=45 ...)')
    fields = OrderedDict((rule, m[0]) for rule, m in margins.items())
    fields['all'] = _all_field(margins)
    report = _report_numeric(ctx, items, direction, points)
    return ManufacturingAnalysis(part if isinstance(part, Shape) else shape, items, fields, {r: (m[1], m[2]) for r, m in margins.items()}, report,
                                 len(points), direction, time.time() - t0)


# ---------------------------------------------------------------------------
# The shell: remedies.  Each is a field made from the part's own -- shifted copies of it -- and the remedies are all made from the part as it
# is (not from one another: shifted copies of shifted copies would multiply), then united: what a remedy adds, the union of what they add,
# and what they cut, taken off that.  Giving the result to the shell again is the next pass.

REMEDIES = ('undercut', 'draft', 'gaps', 'corners', 'walls', 'overhang')

_REMEDY_TEXT = OrderedDict([
    ('undercut', _tr('undercuts filled')),
    ('draft', _tr('draft added to the walls')),
    ('gaps', _tr('narrow gaps closed or widened')),
    ('corners', _tr('inside corners rounded')),
    ('walls', _tr('thin walls thickened')),
    ('overhang', _tr('overhangs made to stand')),
])
_REMEDY_RULE = {'undercut': 'undercut_along', 'draft': 'draft', 'gaps': 'min_gap', 'corners': 'min_radius', 'walls': 'min_wall', 'overhang': 'overhang'}

# What the fillet of a smoothing is: smooth(f, radius) rounds an inside corner of 90 degrees to about 1.14 times its radius; the smoothing has
# a radius of this share of the radius asked
_CORNER_SHARE = 1.0 / 1.14


def _extent_along(ctx, d):
    return sum(abs(d[i]) * (ctx.hi[i] - ctx.lo[i]) for i in range(3)) + 1e-6


def _remedy_walls(ctx, t):
    ''' Where a wall is thinner than `t` the surface is offset outwards by what it lacks, half on each side: how thick the wall is at a point is found
        from how deep a ball can sit inside it on the line along the normal from the nearest point of the surface (the middle of a wall of thickness T
        is T/2 deep), and the offset is a field of its own -- the part's field minus it -- so a wall grows only where it is thin '''
    n, f = ctx.normals, ctx.field
    probes = []
    for k in range(1, 7):
        along = f + t * k / 6.0
        probes.append(-_at(f, -along * n.x, -along * n.y, -along * n.z))
    d = (_join('max', probes, ctx.tie) / n.scale).max(0)
    gate = ((t * (1.0 - GRACE) - _chord(ctx, t, True)) * (1.0 / (0.05 * t))).max(0).min(1)        # (a ball sits badly in a corner of a thick body: act only where the line along the normal says the wall is thin too)
    delta = ((t - 2.0 * d) * (0.5 * (1.0 + GRACE))).max(0).min(t / 2.0) * gate
    return f - _near(delta, f, 1.5 * t, t / 2.0)


def _remedy_gaps(ctx, g, mode):
    ''' A gap narrower than `g` is closed ('fill': the surface of each side grows by half the gap, a little more) or made as wide as `g` ('widen':
        each side goes back by half of what it lacks).  How wide the air is at a point is found the way a wall's thickness is: how big a ball fits in
        the air on the line along the normal from the nearest point of the surface '''
    n, f = ctx.normals, ctx.field
    probes = []
    for k in range(1, 7):
        along = f - g * k / 6.0
        probes.append(_at(f, -along * n.x, -along * n.y, -along * n.z))
    d = (_join('max', probes, ctx.tie) / n.scale).max(0)
    gate = ((g * (1.0 - GRACE) - _chord(ctx, g, False)) * (1.0 / (0.05 * g))).max(0).min(1)        # (the same: the air is narrow along the normal too)
    if mode == 'widen':
        return f + _near((g / 2.0 - d).max(0).min(g / 2.0) * gate, f, 1.5 * g, g / 2.0)
    return f - _near((d * 1.1 * gate).min(g), f, 1.5 * g, g)


def _remedy_corners(ctx, radius):
    ''' The part closed by a ball of `radius`: grown by the ball (the field less the radius) and taken back by it (the largest value of that over the ball, at 42
        points a radius away all round, and at the centre).  Taken together: the part united with the largest the field comes to within a radius of each point,
        less the radius.  What a ball of that size reaches is as it was -- a flat face, a bump and an outside corner are not moved -- and an inside corner it cannot
        reach into is filled to the radius of the ball, along the whole of the corner (not less near its ends: no filter of the field over the points round a point
        is made).  It is what the check measures, so that what it fills it passes.  The field should be about a distance '''
    f = ctx.field
    probes = [f]
    for u in _sphere(162):                       # (so many that about twenty are near any plane: fewer make the fillet a polygon of a few sides, and too big)
        probes.append(_at(f, radius * u[0], radius * u[1], radius * u[2]))
    return f.min(_join('max', probes, ctx.tie) - radius)


def _remedy_undercut(ctx, a):
    ''' What lies both under part of the part and over part of it, along `a`, is filled: the field's smallest value along the line above each point, and
        below it, and where both are inside the part the point is made part of it '''
    f = ctx.field
    total = _extent_along(ctx, a)
    ups, downs = [f], [f]
    levels = 16
    for k in range(1, levels + 1):
        s = total * k / float(levels)
        ups.append(_at(f, a[0] * s, a[1] * s, a[2] * s))
        downs.append(_at(f, -a[0] * s, -a[1] * s, -a[2] * s))
    return f.min(_join('min', ups, ctx.tie).max(_join('min', downs, ctx.tie)))


def _remedy_draft(ctx, degrees, d):
    ''' A wall that leans less than `degrees` from the pull direction is offset outwards by what its slope lacks, growing in proportion to how far it is
        below the top of the part: a wall that is vertical comes out wider at the foot, by the tangent of the draft times its height.  Faces that are
        not wall-like (a floor, a top) are not moved '''
    n, f = ctx.normals, ctx.field
    nd = _dot(n, d)
    wall = (1.0 - nd * nd).max(1e-6).sqrt()
    tilt = nd.abs() / wall                                  # (the slope the wall has: 0 for a vertical one)
    lack = (math.tan(math.radians(degrees)) - tilt).max(0)
    height = Shape.X() * d[0] + Shape.Y() * d[1] + Shape.Z() * d[2]
    top = max(x * d[0] + y * d[1] + z * d[2] for x in (ctx.lo[0], ctx.hi[0]) for y in (ctx.lo[1], ctx.hi[1]) for z in (ctx.lo[2], ctx.hi[2]))
    delta = (top - height).max(0) * lack * wall
    return f - delta


def _entry(f, d, sign, total, count=48):
    ''' How far from each point, along `sign` times the direction `d`, the field first goes from positive to 0 or less -- the part, for a point in the air -- found at
        `count` places along the line (nearer ones closer together) and between them by where the field crosses 0: a number that changes smoothly with the point.
        0 where the point is in the part already, and `total` (as far as is looked) where there is no part that way '''
    far = 1.0e5
    tol = 1.0e-5 * total
    previous = f
    cands = [Shape.wrap(far)]
    last = 0.0
    for k in range(1, count + 1):
        s = total * (k / float(count)) ** 2
        v = _at(f, sign * d[0] * s, sign * d[1] * s, sign * d[2] * s)
        # (a step that has the part's surface exactly at its end counts -- 1 at 0, not one half: a sample that lands on a face, as numbers that round do, is not a miss)
        crossing = ((previous * (1.0 / tol)) + 1.0).max(0).min(1) * (((-v) * (1.0 / tol)) + 1.0).max(0).min(1)
        frac = (previous / (previous - v + 1e-9)).max(0).min(1)
        cands.append(last + (s - last) * frac + far * (1.0 - crossing))
        previous, last = v, s
    inside = (f * (1.0 / tol) + 0.5).max(0).min(1)             # (0 where the point is in the part: it is there already)
    return _join('min', cands, max(5e-7 * total, 2e-6)).min(total) * inside


# The discs of the overhang remedy: how many sides each has, and how far in each is, as a share of the reach (close together near the part's surface, where most points are
# decided; every one is a lookup of the part's field, and the mesher walks the lookups of a field for every cell: 625 of them made a render of minutes)
_DISC_SIDES = 8
_DISC_DEPTHS = (0.0, 0.04, 0.12, 0.28, 0.6, 1.0)


def _remedy_overhang(ctx, degrees, d, fix, reach):
    ''' 'fill': the part grown under every overhang at the slope asked -- a point is part of it when a disc, as wide as the slope has run by the time it is up to the
        part (how far above the point the part starts, found smoothly: no levels, so that the slope is a slope and not a stair of flat treads), fits inside the part
        there: a gusset under a ledge, up to a depth of `reach`; 'cut': the same done to the air, the other way up -- the air grown above a channel into a teardrop '''
    f = ctx.field
    tan = 0.98 * math.tan(math.radians(degrees))        # (a disc of a few sides is a little off a disc: its corners have the slope asked, and between them the surface is steeper -- the side that stands)
    k = _DISC_SIDES
    e1, e2 = _frame(d)
    inset = 0.002 * ctx.size + 2 * ctx.step              # (a little way into the part, so that the disc is in it and not on its surface)
    # The plate the print stands on: what is under it is not air to be grown into, nor a part to be grown under, and the part's own bottom, resting on it, is not an overhang
    plate = min(x * d[0] + y * d[1] + z * d[2] for x in (ctx.plate_box[0][0], ctx.plate_box[1][0]) for y in (ctx.plate_box[0][1], ctx.plate_box[1][1])
                for z in (ctx.plate_box[0][2], ctx.plate_box[1][2]))
    above = (Shape.X() * d[0] + Shape.Y() * d[1] + Shape.Z() * d[2]) - plate          # (the height over the plate)
    if fix == 'fill':
        start = _entry(f, d, +1.0, reach) + inset
        sign = 1.0
    else:
        f = f.min(above)                                  # (everything under the plate is part of the part)
        start = _entry(-f, d, -1.0, reach) + inset
        sign = -1.0
    # Discs at several depths, each counted from where the part starts above the point (not from fixed heights: those make the slope a stair of flat treads).  Each depth
    # is a field that changes smoothly with the point, and so does the best of them.  A disc further in is a wider one: it is the one that fits where the part is wide
    # a little further up (a round hole is wide below its top, and a pointed cap above it is made of discs that sit there)
    rings = []
    for fraction in _DISC_DEPTHS:
        s = start + reach * fraction
        points = []
        for j in range(k + 1):
            if j == k:
                ang_cos = ang_sin = 0.0
            else:
                ang = 2.0 * math.pi * j / k
                ang_cos, ang_sin = math.cos(ang), math.sin(ang)
            dx = s * (sign * d[0] + tan * (ang_cos * e1[0] + ang_sin * e2[0]))
            dy = s * (sign * d[1] + tan * (ang_cos * e1[1] + ang_sin * e2[1]))
            dz = s * (sign * d[2] + tan * (ang_cos * e1[2] + ang_sin * e2[2]))
            points.append(_at(f, dx, dy, dz))
        # (the part is where all of the disc is: the largest value is 0 or less -- for the air, the smallest is above 0)
        rings.append(_join('max' if fix == 'fill' else 'min', points, ctx.tie))
    best = _join('min' if fix == 'fill' else 'max', rings, ctx.tie)       # (any depth that fits)
    # (what is grown only matters where there is part above the point, or air below it, within the reach: where there is none the field is the part's, and the mesher is told so at once)
    gate = _within_reach(f if fix == 'fill' else -f, d, sign, reach, ctx.tie)
    if fix == 'fill':
        return ctx.field.min(best.max(-above).max(gate * 1000.0))            # (grown only above the plate)
    return ctx.field.max(best.min(-gate * 1000.0))


def _supports_field(ctx, result, constraints, direction, reach):
    ''' The supports of what the shell made: a block straight under every surface of it that is an overhang (the rule of its constraint, on the result), down to the plate or
        the part below, kept `support_gap` from the part; none where a constraint's `no_support` regions are, or outside the region of the constraint.  How far up the part
        starts above a point is found smoothly (see `_entry`), and the overhang rule is read at the point of the part there: the field is where that is an overhang.  A field,
        negative inside the support: the part and its supports are two bodies of a print '''
    rctx = _Context(result, result._bounds)
    rctx.plate_box = (ctx.lo, ctx.hi)
    x, y, z = _xyz()
    out = None
    for c in constraints:
        d = direction if c.build_direction is None else c.build_direction
        only = ManufacturingConstraint(c.name, {'overhang': c.rules['overhang']}, c.build_direction, c.pull_direction, None, c.overhang_fix, c.gap_fix)
        margin = _rule_margins(rctx, [only], d)['overhang'][0]
        plate = min(px * d[0] + py * d[1] + pz * d[2] for px in (ctx.lo[0], ctx.hi[0]) for py in (ctx.lo[1], ctx.hi[1]) for pz in (ctx.lo[2], ctx.hi[2]))
        above = (x * d[0] + y * d[1] + z * d[2]) - plate
        up = _entry(result, d, +1.0, reach)                       # (how far up the part starts)
        there = margin.remap(x + up * d[0], y + up * d[1], z + up * d[2])
        s = (-there).max(c.support_gap - result).max(-above).max(up - 0.999 * reach)
        for blocked in c.no_support:
            s = s.max(-blocked)
        if c.region is not None:
            s = s.max(c.region)
        out = s if out is None else out.min(s)
    out._bounds = result._bounds
    return out


def _feather(ctx):
    return max(0.01 * ctx.size, 4 * ctx.step)


def _weight(region, eps):
    ''' 1 inside the region, 0 outside it, eased over `eps` '''
    return 1.0 - (region * (1.0 / eps)).max(0).min(1)


def _volume_points(ctx, count, seed=11):
    rng = random.Random(seed)
    pad = 0.05 * ctx.size
    lo = [ctx.lo[i] - pad for i in range(3)]
    hi = [ctx.hi[i] + pad for i in range(3)]
    pts = [(rng.uniform(lo[0], hi[0]), rng.uniform(lo[1], hi[1]), rng.uniform(lo[2], hi[2])) for _ in range(count)]
    box = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])
    return pts, box


class ManufacturingResult(FieldResult):
    ''' The part the shell made, and what it did.  It is a field like the part is -- wherever a body goes it goes (`offset(result, 1)`, `difference`,
        the part of an analysis, an export).  Stated on its own it is shown coloured by what changed: red where material was added, blue where it was
        cut, the part's colour where it is as it was.

            part          the part as it was
            changes       what each remedy did: {remedy, rule, added, removed (mm^3), kept, why}
            added, removed   mm^3 of material added and cut in all (measured on points of space: about, to a few per cent)
            share         the part's volume changed, as a share
            before        the check of the part as it was (a ManufacturingAnalysis)
            after         the check of what the shell made, when it was made (a ManufacturingAnalysis), or None
            fail_states   what is still broken, where: {rule, where, share, why}
            orientation   the way up of the print, when the constraint left it free (the part is not turned: oriented() is the part turned to stand that way)
            change_map    the field the part is coloured by: 1 added, -1 cut, 0 as it was

        What it made is the part's field changed, not a mesh: run `manufacturing_analysis` on it (or `.after`) to see what the check says of it -- it is the
        shell's own check, with the rules it was asked.  Not a substitute for the check of the one who makes it. '''

    def __init__(self, part, field, change_map, changes, volume, before, after, fail_states, direction, free, notes, seconds):
        self.part = part
        self._field = field
        self.change_map = change_map
        self.changes = changes
        self.added = sum(c['added'] for c in changes if c['kept'])
        self.removed = sum(c['removed'] for c in changes if c['kept'])
        self.volume = volume
        self.share = (self.added + self.removed) / volume if volume > 0 else 0.0
        self.before = before
        self.after = after
        self.fail_states = fail_states
        self.orientation = direction if free else None
        self.build_direction = direction
        self.notes = notes
        self.seconds = seconds
        self._shown = None

    def _as_field(self):
        return self._field

    @property
    def fields(self):
        return OrderedDict([('change_map', self.change_map)])

    def oriented(self, shape=None):
        ''' The part turned so that the way up it was made for is z: what a print of it is made of (the part in the model stays as it is).  Another shape -- the supports,
            a no-support region -- is turned the same way '''
        from fieldes.stdlib.transforms import rotate_y, rotate_z
        d = self.build_direction
        phi = math.atan2(d[1], d[0]) if (abs(d[0]) > 1e-12 or abs(d[1]) > 1e-12) else 0.0
        theta = math.acos(max(-1.0, min(1.0, d[2])))
        out = self._field if shape is None else shape
        if abs(phi) > 1e-12:
            out = rotate_z(out, -phi)
        if abs(theta) > 1e-12:
            out = rotate_y(out, -theta)
        return out

    def _display(self):
        if self._shown is None:
            out = colored(self._field, self.change_map, range=(-1.0, 1.0), label=_tr('changed: +1 added, -1 cut'), colormap='turbo')
            out._color_fields = [('change', _tr('what changed: +1 added, -1 cut'), self.change_map, -1.0, 1.0)]
            out._color_field_name = 'change'
            rows = [(_tr('material added'), '%.4g' % self.added, 'mm^3'), (_tr('material cut'), '%.4g' % self.removed, 'mm^3'),
                    (_tr('of the part'), '%.3g %%' % (100.0 * self.share), ''), (_tr('still broken'), '%d' % len(self.fail_states), '')]
            out._key_results = rows
            if getattr(self, 'supports', None) is not None:
                bodies = colored(self.supports, Shape.wrap(0.15), range=(-1.0, 1.0), label=_tr('supports'), colormap='turbo')
                self._shown = [out, bodies]
                return self._shown
            self._shown = out
        return self._shown

    def __repr__(self):
        lines = [_tr('Manufacturing optimization: %.4g mm^3 added, %.4g mm^3 cut (%.2g %% of the part), %.1f s') % (
            self.added, self.removed, 100.0 * self.share, self.seconds)]
        for c in self.changes:
            word = _REMEDY_TEXT[c['remedy']]
            if c['kept']:
                lines.append(_tr('    %s: +%.4g mm^3, -%.4g mm^3') % (word, c['added'], c['removed']))
            else:
                lines.append(_tr('    %s: not made -- %s') % (word, c['why']))
        if self.orientation is not None:
            lines.append(_tr('  way up of the print found: (%.3g, %.3g, %.3g)') % tuple(self.build_direction))
        if self.fail_states:
            lines.append(_tr('  still broken:'))
            for s in self.fail_states[:10]:
                lines.append(_tr('    %s: %s %.2g %% of the surface -- %s') % (_RULE_TEXT[s['rule']][0], _where(s['where']), 100.0 * s['share'], s['why']))
        elif self.after is not None:
            lines.append(_tr('  the check of what was made finds every rule kept'))
        for note in self.notes:
            lines.append('  ' + note)
        lines.append(_tr('  Run the analysis again on the result: this check is the shell\'s own, and not a substitute for the maker\'s.'))
        return '\n'.join(lines)


@_with_conditions
def manufacturing_optimization(part, constraints, keep=None, remedies=None, max_change=0.25, max_support_height=None, verify=True, samples=2500):
    ''' The part, changed until it keeps the rules of its constraints, and what was changed.  Each remedy is a field made from the part's own, in the
        region of its constraint: thin walls thickened (an offset that follows how thin the wall is), a narrow gap closed (`gap_fix='widen'` of the
        constraint: made as wide as asked), an inside corner rounded to the radius, a draft put on the walls, an undercut filled, an overhang made to stand
        by a gusset at the angle asked (`overhang_fix='cut'`: a teardrop cut above a channel).  They are made from the part as it is and united;
        what they changed is measured, and a remedy that would change more of the part than `max_change` allows is not made.  A region that is excluded
        or in `keep` is never touched.

            part         a body
            constraints  manufacturing_constraint(...), fff_constraint(...) ...: one, or a list.  A constraint with a region changes the part only there
            keep         shapes (regions) that stay as they are, besides the regions the part was excluded with
            remedies     which remedies may be made, a list of 'undercut', 'draft', 'gaps', 'corners', 'walls', 'overhang' (default: those the rules ask for)
            max_change   the most of the part's volume that the shell may change, as a share (default 0.25); a remedy that would go past it is not made
            max_support_height  how deep a gusset under an overhang may be, mm (default a fifth of the part's size)
            verify       the shell checks what it made, with the same rules, and says what is still broken and where (`fail_states`); False leaves it to
                         manufacturing_analysis of the result (a big part is slow to check)
            samples      how many points of the surface it checks

        Returns a ManufacturingResult: a body that is the part changed, coloured by what changed, with `.changes`, `.added`, `.removed`, `.fail_states`.
        Run `manufacturing_analysis` on the result: the shell's own check is not a substitute for the maker's. '''
    t0 = time.time()
    items = _constraints_given(constraints, 'manufacturing_optimization')
    shape = Shape.wrap(part)
    ctx = _Context(shape)
    # (a part that is slow to evaluate is not checked again by the shell)
    probe_pts = [(ctx.lo[0] + (ctx.hi[0] - ctx.lo[0]) * (i + 0.5) / 100.0, ctx.lo[1] + 0.3 * (ctx.hi[1] - ctx.lo[1]), ctx.lo[2] + 0.5 * (ctx.hi[2] - ctx.lo[2]))
                 for i in range(100)]
    started = time.time()
    _values(ctx.field, probe_pts)
    slow = (time.time() - started) > 0.25
    notes = []
    _progress(0.02, _tr('reading the part'))
    _log(t0, 'the part is read')
    _progress(0.05, _tr('measuring the surface of the part'))
    before_points = _surface_samples(ctx, int(samples))
    _log(t0, 'sampled the surface of the part: %d points' % len(before_points))
    direction = _direction_of(items, ctx, before_points)
    free = any(c.free_direction for c in items)
    margins = _rule_margins(ctx, items, direction)
    if not margins:
        raise ManufacturingError('manufacturing_optimization: the constraints have no rule -- give one (min_wall=0.8, overhang=45 ...)')
    before_fields = OrderedDict((r, m[0]) for r, m in margins.items())
    before_fields['all'] = _all_field(margins)
    before = ManufacturingAnalysis(shape, items, before_fields, {r: (m[1], m[2]) for r, m in margins.items()}, _report_numeric(ctx, items, direction, before_points),
                                   len(before_points), direction, 0.0)
    allowed = set(remedies) if remedies is not None else set(REMEDIES)
    unknown = allowed - set(REMEDIES)
    if unknown:
        raise ManufacturingError('manufacturing_optimization: no remedy %s -- the remedies are %s' % (', '.join(sorted(unknown)), ', '.join(REMEDIES)))
    eps = _feather(ctx)
    keeps = [r for r in (list(keep) if isinstance(keep, (list, tuple)) else ([keep] if keep is not None else []))]
    for _, region in locks_of(shape):
        keeps.append(region)
    keep_weight = None
    for r in keeps:
        w = _weight(Shape.wrap(r), eps)            # (1 inside: the part stays as it is there)
        keep_weight = w if keep_weight is None else keep_weight.max(w)
    reach = float(max_support_height) if max_support_height else 0.2 * ctx.size
    pts, box = _volume_points(ctx, 12000)
    f0_vals = _values(ctx.field, pts)
    inside0 = [v <= 0 for v in f0_vals]
    volume = box * sum(inside0) / float(len(pts))
    if not volume > 0:
        raise ManufacturingError('manufacturing_optimization: the part has no volume to measure (is it empty, or very thin?)')
    budget = max_change * volume
    _progress(0.2, _tr('measuring the part\'s volume'))
    _log(t0, 'the volume of the part is measured')

    candidates = []                  # (remedy, rule, field, adds)
    for c in items:
        d = direction if c.build_direction is None else c.build_direction
        w_region = None if c.region is None else _weight(c.region, eps)

        def scoped(new, w_region=w_region):
            out = new
            if w_region is not None:
                out = ctx.field + w_region * (out - ctx.field)
            if keep_weight is not None:
                out = out + keep_weight * (ctx.field - out)
            return out
        for rule, value in c.rules.items():
            if rule == 'min_wall' and 'walls' in allowed:
                candidates.append(('walls', rule, scoped(_remedy_walls(ctx, value)), True))
            elif rule == 'min_gap' and 'gaps' in allowed:
                candidates.append(('gaps', rule, scoped(_remedy_gaps(ctx, value, c.gap_fix)), c.gap_fix == 'fill'))
            elif rule == 'min_radius' and 'corners' in allowed:
                candidates.append(('corners', rule, scoped(_remedy_corners(ctx, value)), True))
            elif rule == 'undercut_along' and 'undercut' in allowed:
                candidates.append(('undercut', rule, scoped(_remedy_undercut(ctx, value)), True))
            elif rule == 'draft' and 'draft' in allowed:
                candidates.append(('draft', rule, scoped(_remedy_draft(ctx, value, c.pull_direction)), True))
            elif rule == 'overhang' and 'overhang' in allowed:
                candidates.append(('overhang', rule, scoped(_remedy_overhang(ctx, value, d, c.overhang_fix, reach)), c.overhang_fix == 'fill'))
    changes = []
    adds, cuts = [], []
    spent = 0.0
    for index, (remedy, rule, field, additive) in enumerate(candidates):
        _progress(0.25 + 0.5 * index / max(1, len(candidates)), _tr('measuring what %s changes') % _REMEDY_TEXT[remedy])
        vals = _values(field, pts)
        added = box * sum(1 for a, b in zip(f0_vals, vals) if a > 0 >= b) / float(len(pts))
        removed = box * sum(1 for a, b in zip(f0_vals, vals) if a <= 0 < b) / float(len(pts))
        entry = {'remedy': remedy, 'rule': rule, 'added': added, 'removed': removed, 'kept': False, 'why': ''}
        _log(t0, 'remedy %s measured: +%.4g -%.4g mm^3' % (remedy, added, removed))
        if added + removed < 1e-4 * volume:
            entry['why'] = _tr('nothing to change')
        elif spent + added + removed > budget:
            entry['why'] = _tr('it would change %.2g %% of the part, and %.2g %% is allowed (max_change): a support, or a change of the design, is what it needs') % (
                100.0 * (spent + added + removed) / volume, 100.0 * max_change)
        else:
            entry['kept'] = True
            spent += added + removed
            (adds if additive else cuts).append(field)
        changes.append(entry)
    result = ctx.field
    # (a remedy is the part's own field where it does nothing: the same value as the part, over most of space -- a tie -- so each is kept a few microns apart; see _join)
    for i, a in enumerate(adds):
        result = result.min(a + ctx.tie * (i + 1))
    for i, c in enumerate(cuts):
        result = result.max(c - ctx.tie * (i + 1))
    result._bounds = (tuple(ctx.lo[i] - 0.05 * ctx.size for i in range(3)), tuple(ctx.hi[i] + 0.05 * ctx.size for i in range(3)))
    for name in ('_locks', '_free'):
        v = getattr(shape, name, None)
        if v is not None:
            setattr(result, name, v)
    tol = max(0.05, 2.0 * ctx.step)
    change_map = (ctx.field * (1.0 / tol)).max(-1).min(1)
    supports, blockers = None, []
    supporting = [c for c in items if c.support and 'overhang' in c.rules]
    support_volume = 0.0
    if supporting:
        _progress(0.78, _tr('making the supports'))
        supports = _supports_field(ctx, result, supporting, direction, _extent_along(ctx, direction))
        svals = _values(supports, pts)
        support_volume = box * sum(1 for v in svals if v <= 0) / float(len(pts))
        for c in supporting:
            blockers += c.no_support
        _log(t0, 'supports made: %.4g mm^3' % support_volume)
    after, fails = None, []
    # (an inside corner is rounded to the radius by what the remedy is made of -- the field after it is not a distance, so a ball cannot be measured against it)
    skipped = ('min_radius',) if any(c['remedy'] == 'corners' and c['kept'] for c in changes) else ()
    _log(t0, 'the remedies are united')
    if verify and not slow and (adds or cuts):
        _progress(0.8, _tr('checking what was made'))
        rctx = _Context(result, result._bounds)
        rctx.plate_box = (ctx.lo, ctx.hi)
        pts_after = _surface_samples(rctx, int(samples), seed=13, rounds=1, batch_size=20000, seeds=[s[0] for s in before_points])
        if pts_after:
            m2 = _rule_margins(rctx, items, direction, skip=skipped)
            after_fields = OrderedDict((r, m[0]) for r, m in m2.items())
            after_fields['all'] = _all_field(m2)
            after = ManufacturingAnalysis(result, items, after_fields, {r: (m[1], m[2]) for r, m in m2.items()}, _report_numeric(rctx, items, direction, pts_after, skipped),
                                          len(pts_after), direction, 0.0)
            for rule, r in after.report.items():
                if rule == 'overhang' and supporting:
                    continue             # (an overhang that is left is held up by a support)
                for g in r['regions'][:5]:
                    fails.append({'rule': rule, 'where': g['at'], 'share': g['share'], 'why': _tr('the check still finds it broken')})
    elif verify and slow:
        notes.append(_tr('the part is slow to evaluate, so it was not checked again: run manufacturing_analysis on the result'))
    elif verify and not (adds or cuts):
        for rule, r in before.report.items():
            if rule == 'overhang' and supporting:
                continue             # (an overhang that is left is held up by a support)
            for g in r['regions'][:5]:
                fails.append({'rule': rule, 'where': g['at'], 'share': g['share'], 'why': _tr('no remedy could be made for it')})
    if skipped:
        notes.append(_tr('inside corners are rounded to the radius by what the remedy is made of, and are not measured again'))
    if keep_weight is not None or any(c.region is not None for c in items):
        notes.append(_tr('what is outside a constraint\'s region, and what is kept, is as the part was'))
    _progress(1.0, _tr('done'))
    _log(t0, 'done')
    if supporting:
        notes.append(_tr('the overhangs that were left are held up by block supports: %.4g mm^3, a separate body (result.supports)') % support_volume)
    out = ManufacturingResult(shape, result, change_map, changes, volume, before, after, fails, direction, free, notes, time.time() - t0)
    out.supports = supports
    out.support_volume = support_volume
    out.blockers = blockers
    return out


for _f in (manufacturing_analysis, manufacturing_optimization):
    _f._required = ('constraints',)
