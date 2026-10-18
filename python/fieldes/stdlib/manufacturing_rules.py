'''
Manufacturing, the rules: what a part has to keep to be made, as constraints and the presets of a process (fff_constraint, cnc_constraint, molding_constraint).

This is the SPECIFICATION and nothing is measured here: the check (manufacturing_check.py) and the optimization (manufacturing.py) both read what is asked from these
objects, and each does its own measuring.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import os
import sys
import time
from collections import OrderedDict

from fieldes.i18n import tr as _tr
from fieldes.shape import Shape
from fieldes.stdlib.fea import FeaError, _given, _register_slots

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
    ('overhang', (_tr('Overhang angle'), '°')),
    ('min_wall', (_tr('Wall thickness'), 'mm')),
    ('min_gap', (_tr('Gap width'), 'mm')),
    ('max_bridge', (_tr('Bridge span'), 'mm')),
    ('min_radius', (_tr('Inside corner radius'), 'mm')),
    ('draft', (_tr('Draft angle'), '°')),
    ('undercut_along', (_tr('Undercut'), '')),
])

# What is measured of a rule is allowed to be this much short of its limit before it counts (a share of the limit): what the part's field and
# a few probes of it can tell is not exact, and a wall that is 0.78 thick for 0.8 asked is not what the check is for
GRACE = 0.05

# A block of support stops this many steps of the field inside the edge of the surface it holds up (the normal that decides whether the surface leans is read with a stencil of one step, which straddles the
# part's own edge within a step of it), and a place that is within this many steps and two more of the edge of a support is held by it: nobody supports the last half millimetre of an edge, and a card that
# listed that rim as still broken said "3 places" of a ledge that was held whole (s3)
SUPPORT_EDGE_STEPS = 4.0


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


def _quantity(value, digits=3):
    ''' A number for a report with `digits` significant digits and never an exponent -- 1005, not 1.01e+03; 175 %, not 1.7e+02 -- which the format %g makes of every number that
        has more integer digits than the digits asked '''
    value = float(value)
    return '%.0f' % value if abs(value) >= 10 ** (digits - 1) else '%.*g' % (digits, value)
# Of two limits of the same rule, the one that asks more
_STRICTEST = {'overhang': min, 'max_bridge': min, 'min_wall': max, 'min_gap': max, 'min_radius': max, 'draft': max, 'undercut_along': lambda a, b: a}


def _constraints_given(constraints, what):
    items = _given(constraints, 'constraints', what)
    if not items:
        raise ManufacturingError('%s: give it at least one constraint -- constraints=[fff_constraint()], for instance' % what)
    return items
