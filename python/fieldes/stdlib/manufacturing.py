'''
Manufacturing: constraints a part has to keep to be made, a check of them, and a shell that changes the part until it keeps them.

    from fieldes import *

    printer = fff_constraint(nozzle=0.4)                         # what a printer with a 0.4 mm nozzle can make
    report = manufacturing_analysis(part, constraints=[printer])   # where the part breaks them (coloured on the part), and how much
    better = manufacturing_optimization(part, constraints=[printer])   # the part, changed until it keeps them, and what was changed

The code is in four files, and the check does not share with the shell what finds a fault:

    manufacturing_rules.py   the constraints and the presets: what is asked (nothing is measured)
    manufacturing_field.py   how a part's field is read: moved lookups, normals, values at points (no rule)
    manufacturing_check.py   manufacturing_analysis: finds where the rules are broken, with a measurement of its own for each rule
    manufacturing.py         this file: manufacturing_optimization and its remedies, each finding what it fixes with code of its own, and its result

The check never imports this file, and the remedies call nothing of the check but `_analyse`, to be told what their result still breaks -- so that the check
of a result, made by `manufacturing_analysis`, is a verification of the remedies and not their own reading of themselves.

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

The SHELL makes the field of the part changed by remedies, each a field made of shifted copies of the part's own: a gusset that makes an overhang stand
at the angle asked (the body under it grown at that slope), a teardrop cut for a channel, an offset that follows how thin a wall is, a fill of the gap that is
too narrow, a rounding of the inside corners (the library's fillet), a draft, a fill of an undercut.  They are all made from the part as it is -- shifted copies
of shifted copies would multiply -- and united, what they add and then what they cut; what each changed is measured, and one that would change more than
`max_change` of the part is not made.  Then the check (manufacturing_check.py) is made on what it made, and what it could not fix is said, with where.  A region
that is excluded (exclude) or kept is never touched.  Give the result to the shell again for a second pass.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
import os
import random
import time
from collections import OrderedDict

from fieldes.i18n import tr as _tr
from fieldes.shape import Shape
from fieldes.stdlib.fea import FieldResult, colored, _with_conditions
from fieldes.stdlib.excluded import locks_of
from fieldes.stdlib.fields import fillet as _fillet
from fieldes.stdlib.shapes import box as _box
from fieldes.stdlib.manufacturing_check import ManufacturingAnalysis, _analyse, manufacturing_analysis
from fieldes.stdlib.manufacturing_field import (_BIG, _Context, _Normals, _at, _dot, _extent_along, _frame, _join, _near, _remembered, _tangents, _us_per_point, _values, _where, _xyz)
from fieldes.stdlib.manufacturing_rules import (GRACE, SUPPORT_EDGE_STEPS, ManufacturingConstraint,ManufacturingError, _RULE_TEXT, _constraints_given, _log, _progress, _quantity, _with_bar, _within,
                                                 manufacturing_constraint, fff_constraint, cnc_constraint, molding_constraint)

__all__ = ['manufacturing_constraint', 'fff_constraint', 'cnc_constraint', 'molding_constraint',
           'manufacturing_analysis', 'manufacturing_optimization', 'ManufacturingConstraint', 'ManufacturingAnalysis', 'ManufacturingResult']


# (how far the slope of a part's field may wander over its surface for the field to count as a distance, for the walls and for the taper at the ends of a face: a plate with a hole cut out of it, a union of boxes
# -- 0.07 -- is one, a lattice's or an optimizer's -- 0.5 and more -- is not.  The walls of a part that is a distance are the formula, exact at its edges and at the feet of the walls it grows (a lattice, at a
# 400th of the part, rounds the end of a fin and thins the growth over a millimetre above the floor it stands on: the printer showcase's fin read 1.56 mm thick at its foot, 0.2 % of the surface).  They were
# the lattice's from 0.02 because the formula drew in minutes; the window draws a lattice copy of it now, see `_bake`)
_NEAR_EXACT = 0.15


def _note_lattice(info):
    ''' One line in the log (FIELDES_MFG_TIMING) for a lattice that a remedy made: the nodes, the nodes the part was looked at in, and the seconds -- where the tool's own seconds go '''
    import sys
    name = sys._getframe(1).f_code.co_name
    _log(time.time(), '%s: %d x %d x %d nodes (%.3g mm apart), the part looked at in %d of them: %.1f s sampling, %.1f s in all' % (
        name, info['dims'][0], info['dims'][1], info['dims'][2], info['pitch'], info['looked_at'], info['sampling_seconds'], info['seconds']))


def _clip(v, reach, on=True):
    ''' `v` held between -reach and reach.  The offsets of the calls that the remedies make into the part are held to the longest one that is ever read (see _chord): the box of the call is then
        the cell and a margin, whatever the part's interval is in the cell, and the tape of the part that is called can be shortened for it, as the part's own is.  Only for a part whose field is expensive per point
        (`on`, `_Context.clamp_calls`): for boxes and cylinders, whose interval is tight, it adds clauses and narrows nothing '''
    if not on:
        return v
    return v.max(-reach).min(reach)


def _edge_room(ctx, limit, d):
    ''' 1 where the face that a point's normal leads to goes on, from the foot of the normal, for `d` or more in every direction along the surface; less, down to 0 at the edge, as an edge is nearer:
        the foot -- the nearest point of the surface, by a Newton step with the field's own slope -- is looked at `d` on along the two tangents, both ways, and where the surface is not there the
        field's value is how far short the face falls (up to `d`).  A growth that is multiplied by it runs out over the last `d` of a face in a straight taper, and is a crease for the mesher
        to draw as a clean edge: the gates that it replaces -- the slope of the field at the foot, which drops within a step of an edge --, switched the growth off in a strip a few hundredths of a
        millimetre wide, narrower than a cell of the picture, whose edge the mesher drew as spikes and flakes '''
    # (no taper for a part whose field is a distance: the slope is 1 on every face, the gate of `_on_face` ends the growth at the edge in one straight strip, and a taper of the last 0.17 mm of a fin that is
    # 0.6 thick leaves a strip along its free edges where the wall is under the limit -- 0.65 % of the surface of n7, the independent check's nine lines reading across it out to 0.3 mm --, and measured
    # without it 0.02 %.  The taper is for a field whose slope wanders -- a lattice's --, where the gate switched on and off in patches within a step of the rim)
    if ctx.slope_spread <= _NEAR_EXACT and not ctx.clamp_calls:
        return Shape.wrap(1.0)
    n, f = ctx.normals, ctx.field
    mag = n.magnitude.max(0.7).min(1.4)
    reach = 1.8 * limit + d
    on = ctx.clamp_calls
    k = f / mag
    px, py, pz = -k * n.x, -k * n.y, -k * n.z
    t1, t2 = _tangents(n)
    room = None
    for t in (t1, t2):
        for s in (d, -d):
            r = _at(f, _clip(px + s * t[0], reach, on), _clip(py + s * t[1], reach, on), _clip(pz + s * t[2], reach, on))
            short = d - r.abs().min(d)
            room = short if room is None else room.min(short)
    return room * (1.0 / d)


def _on_face(ctx, tol, normals=None, newton=False, slope_gate=True, shift=None, fshift=None):
    ''' 1 where the line along the normal starts on a flat face: its foot -- the nearest point of the surface that the normal leads to, p - f n -- is on the surface (not `tol` or more
        off it: over the mouth of a slot, over the top of a fin, the line comes down in the air between two edges, or past an end) and the slope of the field there is a distance's, 1:
        at an edge or a corner the slope is the average of the two faces' and less, and a point round the corner leads to the corner, where a wall looks thinner across its diagonal
        than it is: a remedy that went by that would grow a rounded cap past the end of the wall, a lip, where it has to stop at the edge.  The slope is read at the foot by CENTRAL
        differences, which straddle an edge and read less there (forward differences read a convex edge as steeper than a face, root 2: they let every edge through) '''
    n, f = (ctx.normals if normals is None else normals), ctx.field
    reach = 60.0 * tol                                          # (tol is a fiftieth of the limit: the longest offset read is a limit and a fifth)
    on = ctx.clamp_calls
    # (`shift` and `fshift`: the point moved by the vector `shift`, the part's field there, and the normal `normals` of that point: the same question of the foot of the line from there)
    dx, dy, dz = (0.0, 0.0, 0.0) if shift is None else shift
    here = f if fshift is None else fshift
    # (`newton`: the step to the foot is the field's value over its slope -- the field of a lattice is a distance to a few per cent, and a step by the value alone ends off the surface by that much of the step)
    k = here / n.magnitude.max(0.7).min(1.4) if newton else here
    px, py, pz = _clip(-k * n.x, reach, on), _clip(-k * n.y, reach, on), _clip(-k * n.z, reach, on)
    foot = _at(f, dx + px, dy + py, dz + pz)
    near = ((tol - foot.abs()) * (1.0 / tol)).max(0).min(1)
    if not slope_gate:
        return near
    slope = _Normals(f, ctx.step, shift=(dx + px, dy + py, dz + pz)).magnitude
    return near * ((slope - 0.9) * (1.0 / 0.05)).max(0).min(1)


def _chord(ctx, limit, inward, normals=None, shift=None, fshift=None, count=8, newton=False):
    ''' How thick the part is along the normal at each point of its surface -- through the material (`inward`) or through the air -- as the distance from the surface to where the
        line along the normal meets the surface again, found at `count` places along the line (up to a tenth more than `limit`) and between them by where the field crosses 0:
        `limit` times 1.1 where it does not within that.  From the nearest point of the surface, so that it is the same for a point off it.  This is how the REMEDIES find a thin
        wall or a narrow gap; the check measures the same wall by a method of its own (the shortest of nine lines through it, manufacturing_check._num_thickness).  It is made as a
        chain of minima -- every place that is not where the line meets the surface is put 1e5 away -- because that is what the mesher can skip over where the part is thick; the same
        thickness read from the field's value past the surface, with sums and products, was exact and a hundred times slower to draw.  `shift` (a vector of numbers or fields) and
        `fshift` (the part's field at the point moved by it) measure at the point moved by `shift`, with the normal `normals` of that point: what is asked of a face's neighbour
        that lies in the same plane '''
    n, f = (ctx.normals if normals is None else normals), ctx.field
    sign = -1.0 if inward else 1.0
    reach_ = 1.1 * limit
    # (every offset is held to 1.8 limits: where the part's own value is within 0.6 of a limit of the surface, the only place the result is used, the longest is 0.6 + 1.1 limits; the call's box is then the cell and a margin and the
    # tape of the part that is called can be shortened for it, as the part's own is -- an offset of the part's own value, a loose interval in a cell, made the box as wide as the cell's part and the call the whole part, at every corner)
    cap = 1.8 * limit
    flip = 1.0 if inward else -1.0
    dx, dy, dz = (0.0, 0.0, 0.0) if shift is None else shift
    here = f if fshift is None else fshift
    if newton:
        # (the line starts at the nearest point of the surface, which is f / slope away -- by a Newton step -- for a field whose slope is not 1: a lattice's, a bracket's)
        here = here / n.magnitude.max(0.3).min(2.0)
    previous = Shape.wrap(-1e-3 * limit)
    founds = [Shape.wrap(1.0e5)]
    for k in range(1, count + 1):
        s = reach_ * k / float(count)
        s0 = reach_ * (k - 1) / float(count)
        off = here + (-sign) * s
        v = flip * _at(f, dx - _clip(off * n.x, cap, ctx.clamp_calls), dy - _clip(off * n.y, cap, ctx.clamp_calls), dz - _clip(off * n.z, cap, ctx.clamp_calls))
        a, b = previous, v
        crossing = ((a * (-1.0 / (1e-4 * limit))) + 0.5).max(0).min(1) * ((b * (1.0 / (1e-4 * limit))) + 0.5).max(0).min(1)
        frac = ((-a) / (b - a + 1e-9)).max(0).min(1)
        founds.append(s0 + (s - s0) * frac + 1.0e5 * (1.0 - crossing))
        previous = v
    return _join('min', founds, ctx.tie).min(reach_)


def _leaning_margin(ctx, limit, d, creases=False):
    ''' Above 0 where the surface faces down and leans out more than `limit` degrees from the vertical, `d` being the way up -- where a support is wanted --: the angle of the normal
        from the way up, less the limit and the grace of every rule (5 % of it).  Not on the first layer, which rests on the plate.  `creases=False` (what the supports are placed by): a crease, where the normal is a
        mix of two faces, is read as it is, a continuous angle -- a support under it does no harm, and a gate that shut it out put the edge of the support where the stencil of the normal
        met the part's edge, a place that moved with every height; `creases=True` leaves out where the slope of the field is less than a distance's.  The supports are placed by this, and
        not by the check of the part (manufacturing_check), which they are to be checked by '''
    n = ctx.normals
    plate = min(x * d[0] + y * d[1] + z * d[2] for x in (ctx.plate_box[0][0], ctx.plate_box[1][0]) for y in (ctx.plate_box[0][1], ctx.plate_box[1][1])
                for z in (ctx.plate_box[0][2], ctx.plate_box[1][2]))
    height = (_xyz()[0] * d[0] + _xyz()[1] * d[1] + _xyz()[2] * d[2]) - plate
    lean = (-_dot(n, d)).max(0).min(1).asin() * (180.0 / math.pi)
    margin = lean - limit * (1.0 + GRACE) - _BIG * (0.6 - height).max(0)
    if creases:
        margin = margin - _BIG * (1.0 - ((n.magnitude - 0.8) * (1.0 / 0.1)).max(0).min(1))
    return margin


# ---------------------------------------------------------------------------
# The shell: remedies.  Each is a field made from the part's own -- shifted copies of it -- and the remedies are all made from the part as it
# is (not from one another: shifted copies of shifted copies would multiply), then united: what a remedy adds, the union of what they add,
# and what they cut, taken off that.  Giving the result to the shell again is the next pass.

REMEDIES = ('undercut', 'draft', 'gaps', 'corners', 'walls', 'overhang')
# (the order a part that is expensive to read is made in, each remedy from the result of the ones before it: what makes new faces first, what judges faces last)
_CHAIN_ORDER = ('gaps', 'draft', 'undercut', 'overhang', 'walls', 'corners')

_REMEDY_TEXT = OrderedDict([
    ('undercut', _tr('Undercut filled')),
    ('draft', _tr('Draft applied to walls')),
    ('gaps', _tr('Gap closed or widened')),
    ('corners', _tr('Inside corners filleted')),
    ('walls', _tr('Wall thickness increased')),
    ('overhang', _tr('Overhang supported (gusset or teardrop)')),
    ('access', _tr('Undercut filled, inside corners rounded')),          # (the two as one operator on a part that is expensive to read: see _remedy_access)
])
_REMEDY_RULE = {'undercut': 'undercut_along', 'draft': 'draft', 'gaps': 'min_gap', 'corners': 'min_radius', 'walls': 'min_wall', 'overhang': 'overhang'}
_RULE_REMEDY = {rule: remedy for remedy, rule in _REMEDY_RULE.items()}


def _walls_lattice(ctx, t, threshold=None):
    ''' The wall remedy as a field of the kernel (`libfive_field_overhang_lattice` in mode 3: kernel/src/fields/lattice_field.cpp): the formula of `_remedy_walls` -- a wall thinner than `t` is offset outwards by half of what it
        lacks, to `t / 2` at most, where the thickness is the length of the line along the normal from the nearest point of the surface through the material -- worked out on a lattice laid over the part, the part looked
        at in a band round its surface, outside as far as the growth reaches and inside as far as the thickness is looked for; the normal and the thickness are read off the lattice.  The part united with it; None when the
        library has no lattice field, the limit is smaller than three and a half nodes (the lattice cannot hold a layer of that), or the lattice could not be made (the formula does it then).  The lattice is a 400th of
        the part's size, coarser for a part that would need more than thirty million nodes '''
    import ctypes
    from fieldes.ffi import lib
    if getattr(lib, 'libfive_field_overhang_lattice', None) is None:
        return None
    f = ctx.field
    unit = (0.0, 0.0, 1.0)
    e1, e2 = _frame(unit)
    axes = (e1, e2, unit)
    corners = [(x, y, z) for x in (ctx.lo[0], ctx.hi[0]) for y in (ctx.lo[1], ctx.hi[1]) for z in (ctx.lo[2], ctx.hi[2])]
    proj = [[sum(c[i] * ax[i] for i in range(3)) for c in corners] for ax in axes]
    lo_f, hi_f = [min(p) for p in proj], [max(p) for p in proj]
    pitch = max(ctx.size / 400.0, 1e-3)
    for _ in range(4):
        pad = 1.1 * t + 8.0 * pitch
        volume = 1.0
        for i in range(3):
            volume *= hi_f[i] - lo_f[i] + 2.0 * pad
        wider = max(ctx.size / 400.0, (volume / 3.0e7) ** (1.0 / 3.0))
        if abs(wider - pitch) < 1e-9:
            break
        pitch = wider
    if t < 3.4 * pitch:
        # (a lattice of this pitch cannot hold a layer of that.  Where a formula would be asked instead it is the chain's cost -- every remedy after it reads the formula through everything before it, and the mould's
        # walls on the optimized bracket made the corners' sampling 18 times dearer -- so a finer pitch is tried, to three and a half nodes, as far as forty-five million nodes go)
        finer = t / 3.5
        volume = 1.0
        for i in range(3):
            volume *= hi_f[i] - lo_f[i] + 2.0 * (1.1 * t + 8.0 * finer)
        if volume / finer ** 3 > 4.5e7:
            return None
        pitch = finer
    band = 0.6 * t + 3.0 * pitch
    pad = 1.1 * t + 8.0 * pitch
    mins = [lo_f[i] - pad for i in range(3)]
    dims = [int(math.ceil((hi_f[i] + pad - mins[i]) / pitch)) + 1 for i in range(3)]
    origin = [sum(mins[i] * axes[i][c] for i in range(3)) for c in range(3)]
    frame = (ctypes.c_double * 12)(*(origin + list(e1) + list(e2) + list(unit)))
    cdims = (ctypes.c_int * 3)(*dims)
    stats = (ctypes.c_double * 8)()
    known = None
    try:
        from fieldes.app_support import _var_values
        known = _var_values()
    except Exception:
        known = None
    names, values, count = known if known is not None else (None, None, 0)
    # (`slope` carries the share of the target thickness `t` below which a wall is thin -- the rule's limit over the target, which has a margin on it -- see _remedy_walls)
    ratio = min(1.0, max(0.5, float(threshold) / float(t))) if threshold else 1.0
    ptr = lib.libfive_field_overhang_lattice(f.ptr, names, values, count, frame, cdims, pitch, ratio, float(band), float(t), 3, 1.0e30, -1.0e30, stats, None)
    if not ptr:
        return None
    out = ctx.base.min(Shape(ptr))
    out._lattice = {'pitch': pitch, 'nodes': int(stats[2]), 'looked_at': int(stats[3]), 'volume': stats[1], 'seconds': stats[5], 'sampling_seconds': stats[4], 'dims': tuple(dims)}
    _note_lattice(out._lattice)
    return out


def _remedy_walls(ctx, t, threshold=None, growth=False):
    ''' Where a wall is thinner than `threshold` (the rule's limit; `t` when it is not given) the surface is offset outwards by what it lacks of `t` (the target: the limit and a margin), half on each side, and no more: how thick the wall is at a point is the length
        of the line along the normal, from the nearest point of the surface through the material to where it meets the surface again, and the offset is a field of its own
        -- the part's field minus it --, so a wall grows only where it is thin, over the whole of its face, up to its edge and not past it.

        What it does NOT do, by decision and measurement: the faces of a thin wall are grown, the places where two faces meet are not filled.  Along an edge where thin walls meet, and in the
        inside corner where a thin wall stands on a thick face, a notch of the growth's size is left (0.15 mm for a wall of 0.5 asked 0.8).  Terms that filled them -- a strip of the end face
        of another wall taking the growth of the face beside it, a wedge in the inside corner -- were made and measured: they cost three to ten times the offset to draw, left faint ridges and
        slivers 1.2 mm from the edges of walls that were thick enough, and made the field one the viewer could not mesh within the memory of the machine (8 GB at 14 cells to the millimetre
        on an open box; the same box without them: 0.84 GB).  The corner rule (`min_radius`) rounds such corners '''
    # (a part that is expensive to read, or whose field is not a distance (a lattice's, a bracket's: the slope of the field at its surface is not 1), gets the walls from a lattice, as it gets the draft, the undercut, the
    # corners and the overhang: the formula below asks the part about ten times at every point the mesher samples near the surface -- it was the last of those formulas in the mould's drawing of the bracket, and a
    # gyroid drew in 46 s as a formula and in 3 s as a lattice --, and a field whose slope wanders makes its growth ragged along the rim where a lattice meets the face it was cut from.  A part whose field is a distance
    # (boxes, cylinders, anything of exact planes and arcs) keeps the formula, which is exact at its edges where the lattice, at a 400th of the part, rounds the end of a small plate.  A limit smaller than three and
    # a half nodes of the lattice cannot be held by one: the formula does it then.  FIELDES_WALLS_LATTICE=1 forces the lattice for any part (the proofs), 0 forbids it)
    import os
    mode = os.environ.get('FIELDES_WALLS_LATTICE', '')
    if mode == '1' or (mode != '0' and (getattr(ctx, 'clamp_calls', False) or getattr(ctx, 'slope_spread', 0.0) > _NEAR_EXACT)):
        made = _walls_lattice(ctx, t, threshold)
        if made is not None:
            return made
    t0 = t if not threshold else float(threshold)          # (a wall counts as thin below `t0`: a wall the rule tolerates is not made thicker for the margin that the target has)
    f, n = ctx.field, ctx.normals
    chord = _chord(ctx, t, True, newton=True)
    # (the line along the normal from the nearest point of the surface says something only where that point is on the surface -- over the mouth of a slot the normal leads to the air
    # between the two edges, and a gap would be taken for a wall; and where the slope of the field is not a distance's, on the middle of a gap, the normal is no face's.
    # The foot is found with the field's own slope (a lattice's field is a distance to a few per cent only: a step by its value alone ends off the surface, and the growth was switched
    # off where it did, in patches), and the end of a face is not a step of the gate but a taper: the growth runs out over the last part of the face, `_edge_room`)
    # (the slope of the field is no test here: a lattice's field, a bracket's is a distance only roughly -- 0.6 in one place, 1.3 in another -- and a gate on it left the wall ungrown over large patches, white
    # streaks over a gyroid.  Where the foot of the line is on the surface and not at the edge of its face, the normal is the face's; a place with no slope at all, a plateau, has none)
    # (a part whose field IS a distance -- boxes, cylinders, anything of exact planes and arcs -- has a slope of 1 on a face and less at the foot of an edge, so the gate on the slope of the field ends the growth where the
    # face ends, in one straight strip a step wide, which is what `_edge_room` leaves to it for such a part.  Without it a point past the end of a face, whose nearest point is the EDGE, had a line along its normal that
    # ran on through the wall on a diagonal and found it thin, and was grown by the diagonal's share: a bulge that wraps round every free edge of the wall -- the top of a fin lifted above its top face, a lip standing
    # out past its ends, a flare where it meets the floor -- and drawn wavy, as the lines found it a little thicker or thinner with the angle.  For a field whose slope wanders -- a lattice's -- the gate would leave
    # the wall ungrown over patches, and it is not used.  (The gate, with the 0.8 below for the slope of the field at the point itself, is what the walls had from 10-15 to the change of 10-21 that made them right for
    # a field that is not a distance: that change put the gate and the 0.8 out for EVERY part -- not for speed: it is the same lookups at the foot as before -- and `_edge_room`, which stands in for them, makes
    # no taper for a part that is a distance because its comment counts on the gate.  So for such a part nothing ended the growth at the edge of a face.  The part that is a distance is as it was before that change)
    exact = ctx.slope_spread <= _NEAR_EXACT and not ctx.clamp_calls
    sure = ((n.magnitude - (0.8 if exact else 0.3)) * (1.0 / 0.1)).max(0).min(1) * _on_face(ctx, 0.02 * t, newton=True, slope_gate=exact) * _edge_room(ctx, t, 0.2 * t)
    # (a wall the rule tolerates -- under the limit by the grace at most -- is not made thicker, but one that is under that is made the full thickness, and between the two the growth is
    # eased out, to nothing at the limit: the wall that comes out is then always more than the grace above what the check marks.  It used to be eased out BELOW the line the check marks at,
    # so that a wall of 0.76 to 0.78 came out as thick as it was, and the check, which reads a wall along lines a few degrees off the normal, marked it, with a few hundredths of a
    # millimetre of difference between the two ways of measuring: the two met at the same line)
    # (a line along the normal that leaves the material at once -- a chord under a tenth of the limit -- is the line of a point at an EDGE, grazing along the face beside it, and not a wall of
    # nothing: taken for one it grew the layer by half of the limit round the rim of a disc, a razor-thin flange 0.15 mm wide past its radius at the height of each face, and a lip above the
    # rim of an open box.  Counted for nothing below 0.064 of a 0.8 mm limit and in full from 0.096)
    valid = ((chord - 0.08 * t) * (1.0 / (0.04 * t))).max(0).min(1)
    # (a wall is two faces facing each other: the face that the line ends on has to face back at where it began, its normal within about 37 degrees of the way the line goes on, as the check of the
    # result reads a wall.  Without it a flat face that a sheet meets at a glancing angle -- the cut face of a lattice -- had the wedge behind it taken for a thin wall, and was pushed out past the
    # block it was cut from, a thin lip beyond every cut face.  Same as the lattice's, kernel/src/fields/lattice_field.cpp)
    here = n.magnitude.max(0.3).min(2.0)
    far = 1.8 * t
    end = (_clip(-(f / here + chord) * n.x, far, ctx.clamp_calls), _clip(-(f / here + chord) * n.y, far, ctx.clamp_calls), _clip(-(f / here + chord) * n.z, far, ctx.clamp_calls))
    ne = _Normals(f, ctx.step, shift=end)
    facing = ((-(ne.x * n.x + ne.y * n.y + ne.z * n.z) - 0.7) * 10.0).max(0).min(1)
    eased = ((t0 - chord) * (1.0 / (GRACE * t0))).max(0).min(1)
    gate = eased * eased * sure * valid * facing          # (eased out by the square: a wall the rule tolerates is left as it is, to a few thousandths of a millimetre)
    # (the field is a distance only roughly, not exactly: the surface of f - s is about s / slope from the part's, so the shift is made as long again as the slope is -- the slope MIDWAY between the
    # point and its foot on the old surface, which is what a field whose slope falls away from the surface gains on average along the way: the slope at the point alone moved a gyroid's surface only
    # four fifths of the way)
    reach = 1.2 * t
    foot = (_clip(-(f / here) * n.x, reach, ctx.clamp_calls), _clip(-(f / here) * n.y, reach, ctx.clamp_calls), _clip(-(f / here) * n.z, reach, ctx.clamp_calls))
    slope = (here + _Normals(f, ctx.step, shift=foot).magnitude.max(0.3).min(2.0)) * 0.5
    main = ((t - chord) * 0.5).max(0).min(t / 2.0) * gate * slope
    # (the surface moves by at most half the wall, and only where it is within that of the part's: the remedy is the part's own field beyond 0.6 of a wall thickness, which is what lets the
    # mesher skip every cell farther off without working through it)
    # (`growth`: how far the surface moves out at a point, with the width of the band it lies in and the most it can be -- for the optimizer to add to the growth of the other remedies that move the
    # same surface, so that the faces one of them makes are moved by the others)
    if growth:
        return main, 0.6 * t, t / 2.0
    return f - _near(main, f, 0.6 * t, t / 2.0)


def _remedy_gaps(ctx, g, mode, growth=False):
    ''' A gap narrower than `g` is closed ('fill': the surface of each side grows by half the gap) or made as wide as `g` ('widen': each side goes back by half of what it lacks).
        How wide the air is at a point is found the way a wall's thickness is: the length of the line along the normal, from the nearest point of the surface through the air
        to where it meets the surface again, and only where that point is on the surface (not over the mouth of the gap, where the line comes down between two edges): the
        gap is closed up to its mouth and not above it '''
    f = ctx.field

    def grows(n, rank):
        ''' how far each side of the gap goes, at each point, read with the normals `n`; a place where it goes nowhere is put far below 0 (the further for each `rank`), so that it never ties
            with the 0 of the other reading or with the 0 of the clamp that follows '''
        chord = _chord(ctx, g, False, n)
        # (no gate on the slope of the field here: in a slot the slopes of its two sides cancel over the middle of it, which is what is to be filled, and the normal there still leads
        # to the face that is nearest -- only where the foot of the line is off the surface does it say nothing)
        # (all or nothing at the limit less the grace, the ramp is half a per cent wide: a gap that is only partly closed is a slit narrower than it was, and a narrower gap is the worse one)
        # (and all or nothing in `_on_face` as well, from 0.5 to 0.7: it is 0.8 in the middle of a slot and less towards the middle plane, where the foot of the line is a little off the wall, and the
        # product with it made the growth there 0.127 where 0.15 and the overshoot were wanted -- a crest of air 0.02 wide along the middle plane of a filled V-slot, 0.023 above zero)
        gate = ((g * (1.0 - GRACE) - chord) * (1.0 / (0.005 * g))).max(0).min(1) * ((_on_face(ctx, 0.02 * g, n) - 0.5) * 5.0).max(0).min(1)        # (the same: the air is narrow along the normal too)
        if mode == 'widen':
            step = ((g - chord) * 0.5).max(0).min(g / 2.0) * gate
        else:
            # (each side grows by half the gap and by 2 % of the limit more: the two grown faces then cross, and the field is below 0 on the whole width of the slot, middle included --
            # grown by exactly half they met on the middle plane with the field 0 there, a tie that is neither in nor out)
            step = (chord * 0.5 + 0.02 * g).min(g / 2.0) * gate
        return step - 1.0e3 * rank * (1.0 - (step * (1.0 / (0.001 * g))).max(0).min(1))
    # (read twice, with the normals by forward and by backward differences, and the one that grew more is taken: on the middle of a slot the field of the part (the distance to the
    # nearer face) has a kink, and within a step of it a central or a one-sided difference straddles it and leads to the wrong face -- or to none, exactly on the middle -- so that
    # the slot was filled but on a thin plane there, which the mesher, with a grid plane on it, drew as a seam: two pieces, open edges in the export.  Of the two one-sided ones,
    # one always leads to a wall, and the other, wrong, grows less and never more.  The two are combined as how far each side goes, not as two fields: two fields that are the part's
    # own wherever nothing is closed are equal over all of space, and a minimum of equal values is a tie that the mesher settles by trying every way)
    def lifted(kind, rank):
        ''' how far the surface goes where a point of the air is nearer a face than the sides of the gap it lies in -- the point just above the floor of a slot --, read from the point 0.5 `g` above that face, along its normal.
            The line along the normal of such a point goes up out of the gap into the open, so `grows` finds no gap there, and the sides of the slot, which grow where THEY are the nearest face, left a sheet of air over
            the floor, as thick as the slot is wide in the middle (0.34 mm of a slot of 0.7 under a gap rule of 1.0) and closed in by them from above.  Half the limit above the face, the point is nearer a side than the floor
            in any gap that is narrower than the limit, and the question is asked there in the way it is asked of a side (line along its normal, foot on a face); where the gap is, the point under it is part of the result
            (its growth is its own distance and 2 % of the limit).  For a gap shallower than half the limit the lifted point is out in the open and nothing is added -- the V it leaves is open at the top, not closed in '''
        n0 = ctx.normals
        lam = (0.5 * g - f).max(0).min(0.5 * g)
        reach = 0.5 * g
        sh = (_clip(n0.x * lam, reach, ctx.clamp_calls), _clip(n0.y * lam, reach, ctx.clamp_calls), _clip(n0.z * lam, reach, ctx.clamp_calls))
        fl = _at(f, sh[0], sh[1], sh[2])
        nl = _Normals(f, ctx.step, kind, shift=sh)
        chord = _chord(ctx, g, False, nl, shift=sh, fshift=fl, count=6)
        onface = _on_face(ctx, 0.02 * g, nl, shift=sh, fshift=fl)
        gate = ((g * (1.0 - GRACE) - chord) * (1.0 / (0.005 * g))).max(0).min(1) * ((onface - 0.5) * 5.0).max(0).min(1)
        aired = (fl * (1.0 / (0.02 * g))).max(0).min(1)                 # (the lifted point is in the air, not in the other side of a gap narrower than the lift)
        close = ((0.49 * g - f) * (1.0 / (0.01 * g))).max(0).min(1)       # (a point in a gap narrower than the limit less its grace is nearer a face than 0.475 of the limit)
        step = gate * aired * close * (f.max(0) + 0.02 * g)
        return step - 1.0e3 * rank * (1.0 - (step * (1.0 / (0.001 * g))).max(0).min(1))
    step = grows(ctx.one_sided_normals('forward'), 1.0).max(grows(ctx.one_sided_normals('backward'), 2.0)).max(0)
    if mode != 'widen':
        step = step.max(lifted('forward', 3.0)).max(lifted('backward', 4.0)).max(0)
    if mode == 'widen':
        return f + _near(step, f, 0.6 * g, g / 2.0)
    if growth:
        return step, 0.6 * g, g / 2.0
    return f - _near(step, f, 0.6 * g, g / 2.0)


def _remedy_corners(ctx, radius):
    ''' The part with every inside corner rounded to `radius`: a ball of that radius rolled in the air along the part.  The corner is filled by the arc of the ball that rolls in it -- a circle of that radius whose slope
        turns smoothly from one face to the other --, a gap or a slot narrower than the ball is filled up to the arc the ball rests on (the sag between its two edges), and a flat face, a bump and an outside corner are as
        they were.  It is the kernel's ball (`_corner_maxfilter`), for every part: an import, a lattice, a pocket, a small block and the topology-optimized bracket alike, a field that is made once and costs next to
        nothing to read again.  The library's `fillet` -- the same ball walked in the part's own field, a few dozen calls of it at every point the mesher samples -- was the rounding of a small part and drew slowly;
        it rounded the corners right and filled a gap narrower than the ball wrongly (a plateau 0.8 mm above its mouth with a sharp corner where it meets the wall, and a void in the bottom of a narrow deep slot).
        It stays the fall-back of a small part when no lattice can be made; a part that is expensive to read is then left as it is, and the card says so '''
    made = _corner_maxfilter(ctx, radius)
    if made is not None:
        return made
    if getattr(ctx, 'clamp_calls', False):
        ctx.corners_left = radius          # (no lattice could be made: the corners are left as they are and the tool says so)
        return ctx.field
    # (what a ball adds -- the closing of the part -- never lies beyond the part's convex hull, so beyond its box: the fill is held to it)
    return ctx.field.min(_fillet(ctx.walk, radius, bounds=(ctx.lo, ctx.hi)).max(_box(ctx.lo, ctx.hi)))


def _corner_maxfilter(ctx, radius):
    ''' The inside corners rounded to `radius`, as a field of the kernel with no walk (`libfive_field_overhang_lattice` in mode 5: kernel/src/fields/lattice_field.cpp).  A point of the air is filled when no ball of the
        radius that lies in the air covers it: the largest value of the part's field on the ball round the point falls short of what open air gives (the value plus the radius), by a deficit that is how deep the
        fill is there, and the surface of the rounded part is where the value less the deficit is 0 -- the rolling ball's own arc (the apex in a corner of 90 degrees at 0.293 r from both walls).  The part's field is
        sampled once on a lattice, and the kernel makes of it a TRUE distance (the field of a union of boxes is the largest of the distances to the planes of each box, more than the distance beyond an edge, and a ball
        rolled on that was a fifth short in volume): the nodes that straddle the surface are sources, each with the foot of the perpendicular from it to the surface, a distance transform finds the nearest source
        of every node, and the distance is to that source's foot.  The largest value on the ball is then found at 96 directions and refined round the best of them, to a few hundredths of a millimetre; a tolerance of
        a part of a node keeps a flat face and a convex edge exactly as they are; and the value of the ball's field is divided by its own slope near the surface, so that it is a distance to the first order like the part's.
        There is no walk of the part at every point: the formula (`fillet`) asks the part about 32 times at every point the mesher samples, and on the bracket that took 145 s of the coarse level of the drawing for 62 mm^3.
        The part united with it.  None when the library has no lattice field or the lattice could not be made.  The lattice is a 400th of the part's size, coarser for a part that would need more than thirty million
        nodes; a distance read by trilinear interpolation is good to a small part of a node, so it holds a ball smaller than the lattice's pitch '''
    import ctypes
    from fieldes.ffi import lib
    if getattr(lib, 'libfive_field_overhang_lattice', None) is None:
        return None
    f = ctx.field
    unit = (0.0, 0.0, 1.0)
    e1, e2 = _frame(unit)
    axes = (e1, e2, unit)
    corners = [(x, y, z) for x in (ctx.lo[0], ctx.hi[0]) for y in (ctx.lo[1], ctx.hi[1]) for z in (ctx.lo[2], ctx.hi[2])]
    proj = [[sum(c[i] * ax[i] for i in range(3)) for c in corners] for ax in axes]
    lo_f, hi_f = [min(p) for p in proj], [max(p) for p in proj]
    pitch = max(ctx.size / 400.0, 1e-3)
    for _ in range(4):
        pad = 2.0 * radius + 8.0 * pitch
        volume = 1.0
        for i in range(3):
            volume *= hi_f[i] - lo_f[i] + 2.0 * pad
        wider = max(ctx.size / 400.0, (volume / 3.0e7) ** (1.0 / 3.0))
        if abs(wider - pitch) < 1e-9:
            break
        pitch = wider
    # (the ball is the radius asked and no more: the kernel reads a true distance now, so there is no half node to add -- it moved the bisector of a corner of 60 degrees by 0.17 mm, twice the half node)
    band = 1.0 * radius + 3.0 * pitch
    pad = 2.0 * radius + 8.0 * pitch
    mins = [lo_f[i] - pad for i in range(3)]
    dims = [int(math.ceil((hi_f[i] + pad - mins[i]) / pitch)) + 1 for i in range(3)]
    origin = [sum(mins[i] * axes[i][c] for i in range(3)) for c in range(3)]
    frame = (ctypes.c_double * 12)(*(origin + list(e1) + list(e2) + list(unit)))
    cdims = (ctypes.c_int * 3)(*dims)
    stats = (ctypes.c_double * 8)()
    known = None
    try:
        from fieldes.app_support import _var_values
        known = _var_values()
    except Exception:
        known = None
    names, values, count = known if known is not None else (None, None, 0)
    ptr = lib.libfive_field_overhang_lattice(f.ptr, names, values, count, frame, cdims, pitch, 1.0, float(band), float(radius), 5, 1.0e30, -1.0e30, stats, None)
    if not ptr:
        return None
    # (the lattice holds the exact distance to the surface of the result, as far out as its band.  Outside the original part that is the field of the result; the part's plain field stands only inside the original part, where
    # it keeps a feature thinner than a node of the lattice that the lattice did not see, and is the distance it is there.  United as they were -- the part's field with the ball's -- the part's plain field
    # stood in the air wherever it was smaller than the true distance: a union of a box and a cylinder is the largest of their distances, 2.5 at the axis of a hole in a plate where the rim is 3.0 away, and the
    # ball resting on the rim of a hole read 2.5 and was marked.  A closing never lies beyond the convex hull of the part, so beyond its box)
    inside = ctx.base + 1.0e3 * (ctx.base.max(0) * 1.0e6).min(1)
    out = Shape(ptr).max(_box(ctx.lo, ctx.hi)).min(inside)
    out._full = True          # (this is the result's own field, the part's included: the shell does not unite it with the part's plain field again)
    out._lattice = {'pitch': pitch, 'nodes': int(stats[2]), 'looked_at': int(stats[3]), 'volume': stats[1], 'seconds': stats[5], 'sampling_seconds': stats[4], 'dims': tuple(dims)}
    _note_lattice(out._lattice)
    return out


def _remedy_access(ctx, axis, radius, taper=0.0, both=True):
    ''' The undercut fill and the rounding of the inside corners as ONE operator, a field of the kernel (`libfive_field_overhang_lattice` in mode 7, or 6; kernel/src/fields/lattice_field.cpp): the part with all
        the air filled that a ball-end tool of `radius` cannot touch when it comes along `axis` (from either end, or from the top of the axis only), its shaft as wide as the ball and widening by `taper` (the tangent
        of a draft) for every unit it goes on out.  Done one after another, the fill's vertical walls are sharp inside corners that the rounding must then round, and the fillets' webs cap columns of air that the fill
        had left open; a second pass of each undid some of the last, and the shares of the surface that broke a rule did not go down (undercut 1.3 to 3 %, radius 5 to 10 %, over five rounds).  Here what is left of the
        air is the union of the balls that fit and can go on out, so every point of the surface of the result has a ball resting on it and a line along the axis that meets no material, and a second pass changes
        nothing.  The lattice is a 400th of the part's size (coarser for a part that would need more than twenty-five million nodes); the part's field is made a true distance on it first.  The part united with
        the result as the corners' ball is (the part itself where it is material).  None when the library has no lattice field or the lattice could not be made '''
    import ctypes
    from fieldes.ffi import lib
    if getattr(lib, 'libfive_field_overhang_lattice', None) is None or not radius > 0:
        return None
    norm = math.sqrt(sum(float(c) ** 2 for c in axis)) or 1.0
    unit = tuple(float(c) / norm for c in axis)
    e1, e2 = _frame(unit)
    axes = (e1, e2, unit)
    corners = [(x, y, z) for x in (ctx.lo[0], ctx.hi[0]) for y in (ctx.lo[1], ctx.hi[1]) for z in (ctx.lo[2], ctx.hi[2])]
    proj = [[sum(c[i] * ax[i] for i in range(3)) for c in corners] for ax in axes]
    lo_f, hi_f = [min(p) for p in proj], [max(p) for p in proj]
    pitch = max(ctx.size / 400.0, 1e-3)
    for _ in range(4):
        pad = 2.0 * radius + 8.0 * pitch
        volume = 1.0
        for i in range(3):
            volume *= hi_f[i] - lo_f[i] + 2.0 * pad
        wider = max(ctx.size / 400.0, (volume / 2.5e7) ** (1.0 / 3.0))
        if abs(wider - pitch) < 1e-9:
            break
        pitch = wider
    band = radius + 4.0 * pitch
    pad = 2.0 * radius + 8.0 * pitch
    mins = [lo_f[i] - pad for i in range(3)]
    dims = [int(math.ceil((hi_f[i] + pad - mins[i]) / pitch)) + 1 for i in range(3)]
    origin = [sum(mins[i] * axes[i][c] for i in range(3)) for c in range(3)]
    frame = (ctypes.c_double * 12)(*(origin + list(e1) + list(e2) + list(unit)))
    cdims = (ctypes.c_int * 3)(*dims)
    stats = (ctypes.c_double * 8)()
    known = None
    try:
        from fieldes.app_support import _var_values
        known = _var_values()
    except Exception:
        known = None
    names, values, count = known if known is not None else (None, None, 0)
    ptr = lib.libfive_field_overhang_lattice(ctx.field.ptr, names, values, count, frame, cdims, pitch, max(float(taper), 1.0e-6), float(band), float(radius), 7 if both else 6, 1.0e30, -1.0e30, stats, None)
    if not ptr:
        return None
    inside = ctx.base + 1.0e3 * (ctx.base.max(0) * 1.0e6).min(1)
    out = Shape(ptr).max(_box(ctx.lo, ctx.hi)).min(inside)
    out._full = True          # (this is the result's own field, the part's included: the shell does not unite it with the part's plain field again)
    out._lattice = {'pitch': pitch, 'nodes': int(stats[2]), 'looked_at': int(stats[3]), 'volume': stats[1], 'seconds': stats[5], 'sampling_seconds': stats[4], 'dims': tuple(dims)}
    _note_lattice(out._lattice)
    return out


def _undercut_lattice(ctx, a):
    ''' The undercut fill as a field of the kernel (`libfive_field_overhang_lattice` with a negative slope: kernel/src/fields/lattice_field.cpp): a lattice laid over the part along `a`, the part sampled at the nodes
        with the mesher's pruning, and in every column the air between the first and the last node of the part filled.  The part united with it; None when the library has no lattice field or the lattice could not
        be made (the formula then does it).  The lattice is a pitch of a 250th of the part's size (coarser for a part that would need more than twelve million nodes): the fill's faces along `a` are the part's own,
        its side walls are within half a node of the edge of what lies over the cavity '''
    import ctypes
    from fieldes.ffi import lib
    if getattr(lib, 'libfive_field_overhang_lattice', None) is None:
        return None
    f = ctx.field
    norm = math.sqrt(sum(float(c) ** 2 for c in a)) or 1.0
    unit = tuple(float(c) / norm for c in a)
    e1, e2 = _frame(unit)
    axes = (e1, e2, unit)
    corners = [(x, y, z) for x in (ctx.lo[0], ctx.hi[0]) for y in (ctx.lo[1], ctx.hi[1]) for z in (ctx.lo[2], ctx.hi[2])]
    proj = [[sum(c[i] * ax[i] for i in range(3)) for c in corners] for ax in axes]
    lo_f, hi_f = [min(p) for p in proj], [max(p) for p in proj]
    pitch = max(ctx.size / 250.0, 1e-3)
    for _ in range(4):
        pad = 3.0 * pitch
        volume = 1.0
        for i in range(3):
            volume *= hi_f[i] - lo_f[i] + 2.0 * pad
        wider = max(ctx.size / 250.0, (volume / 1.2e7) ** (1.0 / 3.0))
        if abs(wider - pitch) < 1e-9:
            break
        pitch = wider
    pad = 3.0 * pitch
    mins = [lo_f[i] - pad for i in range(3)]
    dims = [int(math.ceil((hi_f[i] + pad - mins[i]) / pitch)) + 1 for i in range(3)]
    origin = [sum(mins[i] * axes[i][c] for i in range(3)) for c in range(3)]
    frame = (ctypes.c_double * 12)(*(origin + list(e1) + list(e2) + list(unit)))
    cdims = (ctypes.c_int * 3)(*dims)
    stats = (ctypes.c_double * 8)()
    known = None
    try:
        from fieldes.app_support import _var_values
        known = _var_values()
    except Exception:
        known = None
    names, values, count = known if known is not None else (None, None, 0)
    ptr = lib.libfive_field_overhang_lattice(f.ptr, names, values, count, frame, cdims, pitch, -1.0, 3.0 * pitch, -1.0e30, 0, 1.0e30, -1.0e30, stats, None)
    if not ptr:
        return None
    out = ctx.base.min(Shape(ptr))
    out._lattice = {'pitch': pitch, 'nodes': int(stats[2]), 'looked_at': int(stats[3]), 'volume': stats[1], 'seconds': stats[5], 'sampling_seconds': stats[4], 'dims': tuple(dims)}
    _note_lattice(out._lattice)
    return out


def _remedy_undercut(ctx, a):
    ''' What lies both under part of the part and over part of it, along `a`, is filled: the field's smallest value at the places above each point, and at the places below it, and where both
        are inside the part the point is made part of it.  The places are at fixed heights along `a`, the same for every point (the first and the last just inside the two ends of the part),
        not counted from each point: counted from the point, a sliver of material thinner than the spacing -- the tip of a lip, a thin shelf -- is hit by the points whose count happens to land
        in it, and the fill's edge comes out as a staircase as high as the spacing times the slope of the face (0.4 mm on a 20-degree dovetail); at fixed heights the whole sliver is hit
        or missed together, and the edge of the fill is one straight wall.  A shelf thinner than the spacing (the part's length along `a` over 16) may still lie between two heights '''
    # (a part that is expensive to read gets the fill from a lattice the kernel works out once, like the overhang's: the formula below asks the part 17 times at every point the mesher samples -- the mill's drawing of
    # the optimized bracket did not end in seven minutes.  A part whose field is exact keeps the formula, which is exact at any resolution: the lattice, at a 250th of the part, left 2.4 % of a groove's
    # surface undercut and changed material outside it.  The formula stays for a lattice that cannot be made.  FIELDES_UNDERCUT_LATTICE=1 forces the lattice for any part, 0 forbids it (the proofs))
    import os
    mode = os.environ.get('FIELDES_UNDERCUT_LATTICE', '')
    if mode == '1' or (mode != '0' and getattr(ctx, 'clamp_calls', False)):
        made = _undercut_lattice(ctx, a)
        if made is not None:
            return made
    f = ctx.field
    here = Shape.X() * a[0] + Shape.Y() * a[1] + Shape.Z() * a[2]
    ends = [x * a[0] + y * a[1] + z * a[2] for x in (ctx.lo[0], ctx.hi[0]) for y in (ctx.lo[1], ctx.hi[1]) for z in (ctx.lo[2], ctx.hi[2])]
    low, high = min(ends) + ctx.step, max(ends) - ctx.step
    ups, downs = [f], [f]
    levels = 16
    for k in range(levels + 1):
        h = low + (high - low) * k / float(levels)
        move = h - here                                         # (from the point to the place at height h on its line)
        seen = _at(f, a[0] * move, a[1] * move, a[2] * move)
        ups.append(seen.max(here - h))                          # (a place below the point does not count: its value is held above 0 by how far below it is)
        downs.append(seen.max(h - here))
    return f.min(_join('min', ups, ctx.tie).max(_join('min', downs, ctx.tie)))


def _draft_lattice(ctx, degrees, d):
    ''' The draft as a field of the kernel (`libfive_field_overhang_lattice` in mode 2: kernel/src/fields/lattice_field.cpp): the formula of `_remedy_draft` -- the growth of a wall by the height below the top times what its slope
        lacks -- worked out on a lattice laid over the part along the pull direction `d`, the part looked at in a band round its surface as wide as the draft can move it, the normals read off the lattice.  The part united with
        it; None when the library has no lattice field, the angle is 0, or the lattice could not be made (the formula then does it).  The lattice is a pitch of a 400th of the part's size (coarser for a part that would need more
        than thirty million nodes): the surface of the grown part is placed between nodes by the part's own value, within a few hundredths of a node of the formula's '''
    import ctypes
    from fieldes.ffi import lib
    if getattr(lib, 'libfive_field_overhang_lattice', None) is None or degrees <= 0:
        return None
    f = ctx.field
    norm = math.sqrt(sum(float(c) ** 2 for c in d)) or 1.0
    unit = tuple(float(c) / norm for c in d)
    e1, e2 = _frame(unit)
    axes = (e1, e2, unit)
    corners = [(x, y, z) for x in (ctx.lo[0], ctx.hi[0]) for y in (ctx.lo[1], ctx.hi[1]) for z in (ctx.lo[2], ctx.hi[2])]
    proj = [[sum(c[i] * ax[i] for i in range(3)) for c in corners] for ax in axes]
    lo_f, hi_f = [min(p) for p in proj], [max(p) for p in proj]
    grow, _width = _draft_reach(ctx, degrees, unit)
    pitch = max(ctx.size / 400.0, 1e-3)
    for _ in range(4):
        pad = grow + 7.0 * pitch
        volume = 1.0
        for i in range(3):
            volume *= hi_f[i] - lo_f[i] + 2.0 * pad
        wider = max(ctx.size / 400.0, (volume / 3.0e7) ** (1.0 / 3.0))
        if abs(wider - pitch) < 1e-9:
            break
        pitch = wider
    band = grow + 4.0 * pitch                                   # (the part is looked at out to here: the growth is never more than `grow`, and the normal at a node needs its neighbours)
    pad = band + 3.0 * pitch
    mins = [lo_f[i] - pad for i in range(3)]
    dims = [int(math.ceil((hi_f[i] + pad - mins[i]) / pitch)) + 1 for i in range(3)]
    origin = [sum(mins[i] * axes[i][c] for i in range(3)) for c in range(3)]
    frame = (ctypes.c_double * 12)(*(origin + list(e1) + list(e2) + list(unit)))
    cdims = (ctypes.c_int * 3)(*dims)
    stats = (ctypes.c_double * 8)()
    top = max(proj[2]) - mins[2]                                # (the top of the part up the third axis of the lattice: the height above the first node)
    known = None
    try:
        from fieldes.app_support import _var_values
        known = _var_values()
    except Exception:
        known = None
    names, values, count = known if known is not None else (None, None, 0)
    ptr = lib.libfive_field_overhang_lattice(f.ptr, names, values, count, frame, cdims, pitch, math.tan(math.radians(degrees)), float(band), float(top), 2, 1.0e30, -1.0e30, stats, None)
    if not ptr:
        return None
    out = ctx.base.min(Shape(ptr))
    out._lattice = {'pitch': pitch, 'nodes': int(stats[2]), 'looked_at': int(stats[3]), 'volume': stats[1], 'seconds': stats[5], 'sampling_seconds': stats[4], 'dims': tuple(dims)}
    _note_lattice(out._lattice)
    return out


def _remedy_draft(ctx, degrees, d, growth=False):
    ''' A wall that leans less than `degrees` from the pull direction is offset outwards by what its slope lacks, growing in proportion to how far it is
        below the top of the part: a wall that is vertical comes out wider at the foot, by the tangent of the draft times its height.  Faces that are
        not wall-like (a floor, a top) are not moved '''
    # (a part that is expensive to read -- the optimized bracket -- gets the draft from a lattice the kernel works out once, as the overhang and the undercut get theirs: the formula below asks the part seven times at
    # every point the mesher samples near its surface, and the mould's drawing of the bracket did not end in seven minutes.  FIELDES_DRAFT_LATTICE=1 forces the lattice for any part (the proofs), 0 forbids it)
    import os
    mode = os.environ.get('FIELDES_DRAFT_LATTICE', '')
    if mode == '1' or (mode != '0' and getattr(ctx, 'clamp_calls', False)):
        made = _draft_lattice(ctx, degrees, d)
        if made is not None:
            return made
    f = ctx.field
    height = Shape.X() * d[0] + Shape.Y() * d[1] + Shape.Z() * d[2]
    top = max(x * d[0] + y * d[1] + z * d[2] for x in (ctx.lo[0], ctx.hi[0]) for y in (ctx.lo[1], ctx.hi[1]) for z in (ctx.lo[2], ctx.hi[2]))
    grow, width = _draft_reach(ctx, degrees, d)

    def grows(n):
        ''' how far the surface moves out at each point, read with the normals `n` '''
        nd = _dot(n, d)
        wall = (1.0 - nd * nd).max(1e-6).sqrt()
        tilt = nd.abs() / wall                                  # (the slope the wall has: 0 for a vertical one)
        lack = (math.tan(math.radians(degrees)) - tilt).max(0)
        return (top - height).max(0) * lack * wall
    n0 = ctx.normals
    delta = grows(n0)
    # (on the middle plane of a slot narrower than the draft makes it the slopes of its two faces cancel, a central difference across it reads no normal there and the growth was 0 in a band a step wide -- a seam
    # of air 0.15 mm wide down the middle of a slot of 0.3 between two moulded blocks, shut in by the faces that had grown into it.  There, and only there (where the gradient is under 0.6 of a distance's: the
    # one-sided readings differ from the central one by a few per cent of the draft's own slope elsewhere, and the check read a face of a block at 1.87 degrees that is at 2), the normals are read by forward
    # and by backward differences and the larger growth is taken, as the gap remedy does: of the two one always leads to a face, and the other leads to the other face of the slot or to the same one)
    kink = ((0.6 - n0.magnitude) * (1.0 / 0.3)).max(0).min(1)
    delta = delta.max(grows(ctx.one_sided_normals('forward')).max(grows(ctx.one_sided_normals('backward'))) - 1.0e3 * (1.0 - kink)).max(0)
    # (away from the surface of a RESULT of this tool the field is not a distance: the band of the pass that made it ends in a ramp that rises seven times as fast as a distance, a few tenths of a millimetre off the face
    # it grew, and there the field has no slope along the pull at all -- the lean the face was given is gone from it --, so a second pass read the ramp as a wall with no draft and grew a skin there: a sliver 0.1 mm
    # thick standing off the face with 0.05 mm of air between (d5's third pass).  Where the gradient is more than a fifth over a distance's the field is not a distance and tells nothing of any face: no growth)
    delta = delta * ((1.5 - n0.magnitude) * (1.0 / 0.3)).max(0).min(1)
    # (the surface moves by at most the part's height along the pull times the tangent of the draft: the remedy is the part's own field farther from the surface than that, and the mesher is told so
    # at once, as it is for the walls and the gaps.  Without the band the normals -- six lookups of the part -- were worked through in every cell of the picture)
    if growth:
        return delta, width, grow
    return f - _near(delta, f, width, grow)


def _draft_reach(ctx, degrees, d):
    ''' (the most the draft can move the surface, the width of the band round the surface that holds it): the part's height along the pull times the tangent of the draft, and a little more than
        that -- the ramp of `_near` must stay above what it limits where the growth is the greatest '''
    norm = math.sqrt(sum(float(x) ** 2 for x in d)) or 1.0
    grow = _extent_along(ctx, [float(x) / norm for x in d]) * math.tan(math.radians(degrees))
    return grow, 1.25 * grow + 2.0 * ctx.step


def _close_feet(ctx, result, d, lift):
    ''' The feet of the grown walls, closed.  A wall that is grown -- thickened, given a draft -- moves out at the points it is the NEAREST face of.  A point just above the floor the wall stands on, between
        the floor and the plane at 45 degrees through the corner, is nearer to the floor than to the wall; the floor does not grow, and nothing grows there: under the grown wall lies a wedge of air as deep as the
        growth (0.4 mm at the foot of a wall 12 high with a draft of 2 degrees), open to the side -- a ledge for the two halves of a mould to miss, at the foot of every wall the draft was given to.  Such a point
        is asked once more from `lift` (the most the surface moves) farther along the pull, away from the floor: there the wall is the nearest face and the result says whether it is inside the grown skin of the wall.
        If it is, the point is part of the result too: the wall's own lean goes on down to the floor, as the walls are grown, with no vertical stripe in place of it.  Asked only of a point whose nearest face is a floor
        (its normal along the pull), of a place that lies in the skin that was grown (outside the part, inside the result) and beside a wall (its normal across the pull): a gap between a floor and a ledge in the
        part, the underside of a thickened ledge and a roof are not it.  The same under a roof, the other way up.  Where the gates are 0 the lookup is held far out and the mesher drops it

        The gate on the point's own normal is wide: a point ON the 45-degree line from the corner, where the two faces are equally near, has a normal that is the average of the wall's and the floor's
        (read by central differences, 45 degrees from the pull), and with the gate at 1 from 0.75 up it was 0.8 there and 0 a little to the side, and the lookup that is held far out unless the gate is
        exactly 1 did not fill: a slit of air 0.1 mm wide along that line, the whole length of the foot (measured on the fin of 01_printer.py: the correction at the foot is the growth, then a gap of 0,
        then a ramp), which the correction lattice that is drawn (`_bake`) read as a neck in the grown wall and a skirt on the floor, 0.3 mm wide and not alike on the two sides.

        The lifted place counts for eight times its value OUTSIDE the skin (and for its value inside it): `min(result, lifted)` pulled the field down in the air beside the foot, to the
        lifted place's distance from the wall less the growth, in a band as wide as the growth along the 45-degree line from the corner -- where nothing is grown, the surface does not move, and the
        value is only smaller --, and the correction lattice read that band as a correction of up to the growth, a ramp that its carry copied under the floor, so that at the floor the lattice
        held a correction of 0.2 to 0.4 mm where the exact result has none: a fillet 0.5 mm wide and 0.5 mm high at the foot of the fin, measured at the 0.34 mm nodes of the showcase and the
        same drawn in cells of 0.1 mm.  The place where the lifted value is 0, the surface, and every sign are as they were: only a band of a twentieth of a millimetre outside the skin holds the lifted value now '''
    norm = math.sqrt(sum(float(x) ** 2 for x in d)) or 1.0
    u = tuple(float(x) / norm for x in d)
    nd = _dot(ctx.normals, u)
    out = result
    for sign in (1.0, -1.0):
        move = tuple(sign * lift * c for c in u)
        floor = ((sign * nd - 0.3) * 4.0).max(0).min(1)                                            # (the nearest face of the point faces away from the floor, or is the average of it and a wall's: 1 from 55 degrees of the pull, 0 from 73)
        skin = (_at(ctx.field, *move) * (1.0 / ctx.step)).max(0).min(1)                              # (the lifted place is outside the part)
        beside = ((0.5 - _dot(_Normals(ctx.field, ctx.step, shift=move), u).abs()) * 4.0).max(0).min(1)    # (and its nearest face is a wall)
        lifted = _at(result, *move)
        out = out.min(lifted + 7.0 * lifted.max(0) + _BIG * (1.0 - floor * skin * beside))
    return out


_BAKE = os.environ.get('FIELDES_BAKE', '') != '0'         # (0: the walls, gaps and draft stay the formula that draws in a minute -- for comparing: see _bake)


def _bake(ctx, formula, widest, most):
    ''' The walls, the gaps and the draft of a part with an exact field -- the formula `ctx.field - _near(...)`, closed at the feet of the grown walls -- as a CORRECTION LATTICE of the kernel
        (`libfive_field_bake_lattice`: kernel/src/fields/lattice_field.cpp, `bakeLattice`).  The formula asks the part's field dozens of times at every point (the foot closing alone ~60 lookups), and the
        mesher needs ~1500 points for each cell on the surface: a surface cell of the mould (showcase 03_mould.py) cost ten milliseconds, 73 seconds for what the part draws in 0.2.  What the formula adds to the part --
        the growth of the walls, 0 away from every face the remedies touch -- is smooth: here it is sampled ONCE, at the nodes of a lattice laid over the part, with the pruning of the mesher (a block of nodes
        that interval arithmetic puts farther than `reach` from the result's surface is not looked at: the correction there is 0), and the result is the part's own field plus that lattice.  The part's faces and
        edges stay exact; the correction is linear along a wall (the draft) and read by trilinear interpolation, exact where it is linear and good to a fraction of a node at its kinks.  The pitch is a third of the
        most the surface moves (at most 0.35 mm, a sixth of the band it lies in, not finer than a 700th of the part, coarser for a lattice of more than twenty-five million nodes).  None when the library has no
        such field or the lattice could not be made (the formula then stays) '''
    import ctypes
    from fieldes.ffi import lib
    if not _BAKE or getattr(lib, 'libfive_field_bake_lattice', None) is None:
        return None
    reach = float(widest) + 2.0 * float(most) + 2.0 * ctx.step              # (the formula changes nothing beyond the band `widest`, and the surface moves by `most` at the most)
    pitch0 = max(ctx.size / 700.0, min(float(most) / 3.0, 0.35, float(widest) / 6.0), 1e-3)
    pitch = pitch0
    for _ in range(4):
        pad = reach + 3.0 * pitch
        volume = 1.0
        for i in range(3):
            volume *= ctx.hi[i] - ctx.lo[i] + 2.0 * pad
        wider = max(pitch0, (volume / 2.5e7) ** (1.0 / 3.0))
        if abs(wider - pitch) < 1e-9:
            break
        pitch = wider
    pad = reach + 3.0 * pitch
    mins = [ctx.lo[i] - pad for i in range(3)]
    dims = [int(math.ceil((ctx.hi[i] + pad - mins[i]) / pitch)) + 1 for i in range(3)]
    frame = (ctypes.c_double * 12)(*(mins + [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]))
    cdims = (ctypes.c_int * 3)(*dims)
    stats = (ctypes.c_double * 8)()
    known = None
    try:
        from fieldes.app_support import _var_values
        known = _var_values()
    except Exception:
        known = None
    names, values, count = known if known is not None else (None, None, 0)
    zone_ptr = ctypes.c_void_p()
    ptr = lib.libfive_field_bake_lattice(formula.ptr, ctx.base.ptr, names, values, count, frame, cdims, pitch, reach, stats, ctypes.byref(zone_ptr))
    if not ptr:
        return None
    out = ctx.base + Shape(ptr)
    out._lattice = {'pitch': pitch, 'nodes': int(stats[2]), 'looked_at': int(stats[3]), 'volume': 0.0, 'seconds': stats[5], 'sampling_seconds': stats[4], 'dims': tuple(dims)}
    _note_lattice(out._lattice)
    return out, (Shape(zone_ptr.value) if zone_ptr.value else None)


def _entry(f, d, sign, total, count=48, with_shadow=False):
    ''' How far from each point, along `sign` times the direction `d`, the field first goes from positive to 0 or less -- the part, for a point in the air -- found at
        `count` places along the line (nearer ones closer together) and between them by where the field crosses 0: a number that changes smoothly with the point.
        0 where the point is in the part already, and `total` (as far as is looked) where there is no part that way.
        `with_shadow`: also the SHADOW of the part along the line -- the smallest value of the field at those places, which is 0 or less where the line meets the part and, to the side of
        what is over the point, the distance to it, growing by one for one: the way to say "is there part over this point" with an edge that is a distance and not a jump (the entry itself
        jumps from how far the part is to `total` at the edge of what is over the point); the first of the two is then the distance to the surface found by walking up to it from the air
        side (see below), which is exact beside the part's edges too, where the crossing drawn between two samples is not '''
    far = 1.0e5
    tol = 1.0e-5 * total
    previous = f
    cands = [Shape.wrap(far)]
    airs = [Shape.wrap(far)]
    lows = [f]
    last = 0.0
    for k in range(1, count + 1):
        s = total * (k / float(count)) ** 2
        v = _at(f, sign * d[0] * s, sign * d[1] * s, sign * d[2] * s)
        lows.append(v)
        # (a step that has the part's surface exactly at its end counts -- 1 at 0, not one half: a sample that lands on a face, as numbers that round do, is not a miss.  A step counts when the line is in the part at
        # its end: that the line was in the air at its start is not asked -- every step after the first crossing would also have counted, but the first is the smallest, so the value is the same at every point -- and
        # the mesher, which works with the range a value can have in a cell, then has a finite top to the range as soon as one step is surely in the part.  With both asked, a cell that a sample height cuts through
        # had the whole reach for its range, and so had everything built on it: 60 % of the volume of a gusset's scene was worked through to the finest cells)
        crossing = (((-v) * (1.0 / tol)) + 1.0).max(0).min(1)
        frac = (previous / (previous - v + 1e-9)).max(0).min(1)
        cands.append(last + (s - last) * frac + far * (1.0 - crossing))
        if with_shadow:
            airs.append(last + previous.max(0) + far * (1.0 - crossing))
        previous, last = v, s
    inside = (f * (1.0 / tol) + 0.5).max(0).min(1)             # (0 where the point is in the part: it is there already)
    found = _join('min', cands, max(5e-7 * total, 2e-6)).min(total) * inside
    if with_shadow:
        # (the crossing between two samples is found by a line drawn between the field's two values: exact where the field falls by one for one along the line, and wrong beside the part's
        # edge, where the field is flat inside the part (the distance to the side face, not the way to the surface): there the crossing comes up to a spacing too far, inside the part, and the
        # surface read at it is the side face, not the underside -- a different one at every height, as the samples fall: the face of fine terraces on the support.  The surface is never
        # nearer than the air before the crossing is wide (the field is a distance); stepping on from there by the field's value, as a sphere is walked to a surface, arrives at it from the air
        # side: at once for a face across the line, in a few steps for one that leans)
        surface = _join('min', airs, max(5e-7 * total, 2e-6)).min(total) * inside
        for _ in range(3):
            surface = (surface + _at(f, sign * d[0] * surface, sign * d[1] * surface, sign * d[2] * surface).max(0)).min(total) * inside
        return surface, _join('min', lows, max(5e-7 * total, 2e-6))
    return found


# (A walk to the part -- eight lookups, each at the place the one before it ended, as a sphere is walked to a surface -- was tried for the entry instead of the 48 places of `_entry`: cheaper to ask
# at a point, and a third slower to draw.  A lookup whose place depends on another lookup has a range as wide as the two together, the ranges of eight in a chain are the product, and the mesher
# could not rule out the cells under the part: on the topology bracket's cube of 30 mm 16,469 leaves in 60.6 thread-seconds with the places, 16,514 in 90.1 with the walk: the drawing is what costs.)


def _remedy_overhang(ctx, degrees, d, fix, reach, keep=None):
    ''' (`keep`: the kept regions of the part as one field, negative inside them, or None: a gusset is not made under a stretch of the part where it would have to cross air inside a kept region -- see the
        top of kernel/src/fields/lattice_field.cpp's `K` --, so that no blade of it is left hanging below the face the region cuts)
        'fill': the part grown under every overhang at the slope asked -- a point of the air is part of it when a disc, as wide as the slope has run by the time it is up to the part (how far above
        the point the part starts), fits inside the part there, at that height or at any of the `reach` above it: a gusset under a ledge, as deep as the ledge is wide and no deeper, up to `reach`;
        'cut': the same done to the air, the other way up -- the air grown above a channel into a teardrop.
        It is worked out ONCE, by the kernel (`libfive_field_overhang_lattice`, kernel/src/fields/lattice_field.cpp), on a lattice laid over the part in the build direction, and kept as a field of its
        own: the part is sampled at the nodes (with the pruning of the mesher), the distance in each slice from the part to the air is found to the edge itself, and in each column the lowest height
        the gusset reaches under each stretch of the part.  The result is the part united with that field.  Asked as a formula at every point the mesher samples, the same rule took about a hundred
        looks at the part for each, and the drawing of the topology bracket six minutes where the part alone takes seconds.  The lattice is a pitch of a 250th of the part's size (coarser for a
        part that would need more than twelve million nodes): the gusset's faces are exact planes, its creases are rounded within a node '''
    import ctypes
    from fieldes.ffi import lib
    if getattr(lib, 'libfive_field_overhang_lattice', None) is None:
        raise ManufacturingError('manufacturing_optimization: this FielDes library is too old for the overhang fix (it has no lattice field)')
    f = ctx.field
    # (the faces of the gusset lean a little less than the slope asked: the side that stands, 5 % under it, 43.5 degrees for 45.  The margin is a window: the flank of a teardrop, a cone that meets a cylinder, keeps the
    # rule on its whole surface at 5 % (0.00 % marked by the independent check; 0.30 % at 2 %), and the fill acts on every face that leans more than the flank does, so a plate at 44 degrees, which keeps the rule
    # (it holds up to the limit and 5 % over it, 47.25), is given a sliver of 0.8 mm^3 under it.  A gate that left the faces that keep the rule alone, decided from the cone at the slope that breaks the rule,
    # was tried in the kernel and taken out: a threshold of a node cuts off the slivers a node thin where a flank runs tangent to a bore, and the teardrop was marked on 0.44 % of its surface)
    tan = 0.95 * math.tan(math.radians(degrees))
    norm = math.sqrt(sum(float(c) ** 2 for c in d)) or 1.0
    unit = tuple(float(c) / norm for c in d)
    cut = fix != 'fill'
    build = tuple(-c for c in unit) if cut else unit      # (the air is grown for a cut: the lattice is built up the other way, and holds the complement)
    e1, e2 = _frame(build)
    axes = (e1, e2, build)
    # The plate the print stands on: what is under it is not air to be grown into, nor a part to be grown under, and the part's own bottom, resting on it, is not an overhang
    plate = min(x * unit[0] + y * unit[1] + z * unit[2] for x in (ctx.plate_box[0][0], ctx.plate_box[1][0]) for y in (ctx.plate_box[0][1], ctx.plate_box[1][1])
                for z in (ctx.plate_box[0][2], ctx.plate_box[1][2]))
    corners = [(x, y, z) for x in (ctx.lo[0], ctx.hi[0]) for y in (ctx.lo[1], ctx.hi[1]) for z in (ctx.lo[2], ctx.hi[2])]
    proj = [[sum(c[i] * a[i] for i in range(3)) for c in corners] for a in axes]
    lo_f, hi_f = [min(p) for p in proj], [max(p) for p in proj]
    pitch = max(ctx.size / 250.0, 1e-3)
    for _ in range(4):
        pad = (3.0 * pitch, 3.0 * pitch, reach + 3.0 * pitch)
        volume = 1.0
        for i in range(3):
            volume *= hi_f[i] - lo_f[i] + 2.0 * pad[i]
        wider = max(ctx.size / 250.0, (volume / 1.2e7) ** (1.0 / 3.0))
        if abs(wider - pitch) < 1e-9:
            break
        pitch = wider
    pad = (3.0 * pitch, 3.0 * pitch, reach + 3.0 * pitch)
    mins = [lo_f[i] - pad[i] for i in range(3)]
    dims = [int(math.ceil((hi_f[i] + pad[i] - mins[i]) / pitch)) + 1 for i in range(3)]
    origin = [sum(mins[i] * axes[i][c] for i in range(3)) for c in range(3)]
    frame = (ctypes.c_double * 12)(*(origin + list(e1) + list(e2) + list(build)))
    cdims = (ctypes.c_int * 3)(*dims)
    stats = (ctypes.c_double * 8)()
    if cut:
        # (everything under the plate is part of the part, a little more than that: its own bottom face rests on the plate, and the air is not grown into it)
        min_z, force_lo, force_hi = -1.0e30, -(plate + 4.0 * ctx.step) - mins[2], 1.0e30
    else:
        min_z, force_lo, force_hi = plate - mins[2], 1.0e30, -1.0e30          # (grown only above the plate)
    known = None
    try:
        from fieldes.app_support import _var_values
        known = _var_values()
    except Exception:
        known = None
    names, values, count = known if known is not None else (None, None, 0)
    ptr = lib.libfive_field_overhang_lattice(f.ptr, names, values, count, frame, cdims, pitch, tan, float(reach), min_z, 1 if cut else 0, force_lo, force_hi, stats, keep.ptr if keep is not None else None)
    if not ptr:
        raise ManufacturingError('manufacturing_optimization: the overhang fix could not be made (lattice of %d x %d x %d nodes, status %d)' % (dims[0], dims[1], dims[2], int(stats[0])))
    field = Shape(ptr)
    out = ctx.base.max(-field) if cut else ctx.base.min(field)
    out._lattice = {'pitch': pitch, 'nodes': int(stats[2]), 'looked_at': int(stats[3]), 'volume': stats[1], 'seconds': stats[5], 'sampling_seconds': stats[4], 'dims': tuple(dims)}
    _note_lattice(out._lattice)
    return out


def _supports_field(ctx, result, constraints, direction, reach):
    ''' The supports of what the shell made: a block straight under every surface of it that is an overhang (the rule of its constraint, on the result), down to the plate or
        the part below, kept `support_gap` from the part; none where a constraint's `no_support` regions are, or outside the region of the constraint.  How far up the part
        starts above a point is found smoothly (see `_entry`), and the overhang rule is read at the point of the part there: the field is where that is an overhang.  A field,
        negative inside the support: the part and its supports are two bodies of a print '''
    rctx = _Context(result, result._bounds)
    rctx.plate_box = ctx.plate_box
    x, y, z = _xyz()
    out = None
    for c in constraints:
        d = direction if c.build_direction is None else c.build_direction
        margin = _leaning_margin(rctx, c.rules['overhang'], d)
        plate = min(px * d[0] + py * d[1] + pz * d[2] for px in (ctx.plate_box[0][0], ctx.plate_box[1][0]) for py in (ctx.plate_box[0][1], ctx.plate_box[1][1])
                    for pz in (ctx.plate_box[0][2], ctx.plate_box[1][2]))
        above = (x * d[0] + y * d[1] + z * d[2]) - plate
        up, shadow = _entry(result, d, +1.0, reach, with_shadow=True)      # (how far up the part starts; and the part's shadow: 0 or less where there is part over the point, the distance to it beside)
        there = margin(x + up * d[0], y + up * d[1], z + up * d[2])
        # (the edge of the support beside what is over it is the shadow's, a distance that grows by one for one and is 0 at the part's side face; the distance up to the part jumps from how far it is to
        # `reach` at that face, and a gate on the creases of the part moved the edge with the height by a tenth of a millimetre: the face of the support was drawn as a torn lattice)
        # (the support ends a few steps of the field inside the part's side face -- 4 x 0.043 = 0.17 mm on a part of 43 mm --: the normal that decides whether the surface above leans is read with a stencil of
        # one step, which straddles the part's own edge within one step of it, and where the edge of the support was left to that reading it wandered by tens of microns with the height: a face of fine terraces)
        s = (-there).max(c.support_gap - result).max(-above).max(shadow + SUPPORT_EDGE_STEPS * rctx.step)
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


def _as_offset(cx, made, w_region, keep_weight, scoped, together):
    ''' What a remedy made, as (its field, its growth).  A remedy that moves the surface along its normal -- the walls, the draft, a gap filled -- hands over (how far it moves out at a point, the width of the band it lies
        in, the most it can be) when asked for `growth`: the field is the part's less that, scoped to the constraint's region and to what is kept, and the growth is returned as well when the optimizer adds the growths of
        these remedies (`together`).  A remedy that made a field -- a lattice of the kernel -- is that field, and has no growth '''
    if not isinstance(made, tuple):
        return scoped(made), None
    grow, width, most = made
    if w_region is not None:
        grow = grow * w_region
    if keep_weight is not None:
        grow = grow * (1.0 - keep_weight)
    return cx.field - _near(grow, cx.field, width, most), ((grow, width, most) if together else None)


_HALTON_UNIT = []


def _halton(count, shift, start=0):
    ''' `count` points of the unit cube, from the point `start` of the Halton sequence (bases 2, 3 and 5), each moved by `shift` (a point of the cube; round the cube's faces).  The points of a
        volume measured on them -- the share of them that are in it, times the box -- are spread evenly whatever number of them there is: any run of the sequence is, so that a measurement
        that stops early is still one.  The error of a volume measured on them is a few times smaller than on as many random points (a fillet of 154 mm^3 in a box of 21600, on 57000 points:
        5 % on random points).  The shift moves the points together, so that a part with its faces on round numbers does not line up with them '''
    end = start + count
    while len(_HALTON_UNIT) < end:            # (the points are the same for every shift: made once, as far as anything has asked for)
        i = len(_HALTON_UNIT) + 1
        p = []
        for base in (2, 3, 5):
            f, r, n = 1.0, 0.0, i
            while n > 0:
                f /= base
                r += f * (n % base)
                n //= base
            p.append(r)
        _HALTON_UNIT.append(tuple(p))
    s0, s1, s2 = shift
    return [((u + s0) % 1.0, (v + s1) % 1.0, (w + s2) % 1.0) for u, v, w in _HALTON_UNIT[start:end]]


def _volume_points(ctx, count, seed=11):
    rng = random.Random(seed)
    pad = 0.05 * ctx.size
    lo = [ctx.lo[i] - pad for i in range(3)]
    hi = [ctx.hi[i] + pad for i in range(3)]
    shift = (rng.random(), rng.random(), rng.random())
    pts = [(lo[0] + u * (hi[0] - lo[0]), lo[1] + v * (hi[1] - lo[1]), lo[2] + w * (hi[2] - lo[2])) for u, v, w in _halton(count, shift)]
    box = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])
    return pts, box


def _shell_points(ctx, width, count=40000, seed=17, batches=None):
    ''' Points of the shell round the part's surface -- where its field is within `width` of 0 -- and the volume each stands for: (points, the part's field at them, volume),
        or None when there are none.  A remedy made with that width changes nothing outside the shell, so what it changes is measured on points that are all in it: random
        points over the whole box find a layer a tenth of a millimetre thick on a part of a hundred only a few times in a thousand, and a slot of 56 mm^3 in 175000 not at all.
        The points are drawn evenly over the box (_halton) and the ones in the shell kept: each stands for the box's volume over the number drawn.  Stops at `count` points, or
        after `batches` batches of 30000 (a part whose field is slow to evaluate: three for a part, one for a result of the tool, which costs far more a point; given by what the part is and not by a clock, so
        that the same part is sampled by the same points on a quiet machine and on a busy one) '''
    rng = random.Random(seed)
    shift = (rng.random(), rng.random(), rng.random())
    pad = max(0.05 * ctx.size, width)            # (the shell reaches `width` past the part: a box that stops short of it would leave out the outer part of what a remedy adds)
    lo = [ctx.lo[i] - pad for i in range(3)]
    hi = [ctx.hi[i] + pad for i in range(3)]
    box = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])
    kept, values, drawn = [], [], 0
    batch = 30000
    if batches is None and ctx.clamp_calls:
        batches = 1 if getattr(ctx, '_from_result', False) else 3
    while len(kept) < count and drawn < 6000000 and (drawn == 0 or batches is None or drawn < batches * batch):
        pts = [(lo[0] + u * (hi[0] - lo[0]), lo[1] + v * (hi[1] - lo[1]), lo[2] + w * (hi[2] - lo[2])) for u, v, w in _halton(batch, shift, drawn)]
        vals = _values(ctx.field, pts)
        for p, v in zip(pts, vals):
            if abs(v) < width:
                kept.append(p)
                values.append(v)
        drawn += batch
    if not kept:
        return None
    return kept, values, box / float(drawn)


def _cover_boxes(points, cell, radius, cap):
    ''' Boxes that together cover the cells of a grid (`cell` wide) that hold one of `points`, and the cells within `radius` cells of those: the cells are swept in order and each box is a run along x, grown
        along y while every cell of the run has the same neighbour, and along z while every cell of that rectangle has.  The walls of a part make thin shells; a box for each connected group of points would be one
        box for the whole of a part whose walls meet.  At most `cap` boxes: the cells are made twice as wide until the sweep needs few enough '''
    while True:
        held = set()
        for p in points:
            held.add((int(math.floor(p[0] / cell)), int(math.floor(p[1] / cell)), int(math.floor(p[2] / cell))))
        if radius > 0:
            grown = set()
            for (i, j, k) in held:
                for di in range(-radius, radius + 1):
                    for dj in range(-radius, radius + 1):
                        for dk in range(-radius, radius + 1):
                            grown.add((i + di, j + dj, k + dk))
            held = grown
        used = set()
        boxes = []
        for (i, j, k) in sorted(held):
            if (i, j, k) in used:
                continue
            i1 = i
            while (i1 + 1, j, k) in held and (i1 + 1, j, k) not in used:
                i1 += 1
            j1 = j
            while all((a, j1 + 1, k) in held and (a, j1 + 1, k) not in used for a in range(i, i1 + 1)):
                j1 += 1
            k1 = k
            while all((a, b, k1 + 1) in held and (a, b, k1 + 1) not in used for a in range(i, i1 + 1) for b in range(j, j1 + 1)):
                k1 += 1
            for a in range(i, i1 + 1):
                for b in range(j, j1 + 1):
                    for c in range(k, k1 + 1):
                        used.add((a, b, c))
            boxes.append(((i * cell, j * cell, k * cell), ((i1 + 1) * cell, (j1 + 1) * cell, (k1 + 1) * cell)))
        if len(boxes) <= cap:
            return boxes
        cell *= 2.0


def _scope_field(ctx, groups, before, reach=0.0):
    ''' A field above 0 inside boxes round the places where the remedies that move the surface (the walls, a gap filled, the draft) do anything, and below 0 everywhere else; None where the boxes would hold
        so much of the part that nothing is gained.  The places are those of the points that each remedy was measured on, where it moved the surface at all (`groups`: the points, the spacing of
        them, the most the remedy moves it), and the boxes of the places where the independent check of the part found a wall or a gap broken.  Outside them the remedy is 0 -- its gate finds no thin wall,
        no narrow gap, nothing that leans too little -- and the field says so to the mesher in a way it can use: a formula that finds a thin wall by a walk along the normal (the foot of the line moves with the
        cell, the walk with it) has no interval that is 0 near any face, so every cell within the band of the remedy round the whole surface was worked through to the finest level; a plate of 70 mm drew for
        eleven minutes where its thin fin and its narrow slot were 5 % of it.  A box is grown by a spacing and a half of the points that found it, and by the most that the remedy moves the surface '''
    boxes = []
    for points, spacing, most, width in groups:
        if points:
            # (the growth is a field of its own on both sides of a face: it is not 0 inside the material either, out to the band of the sum of the remedies (`reach`: the ramp of `_near` ends there), and a box that
            # stopped short of that left the part's value there changed by a hundredth of a millimetre where the box ended -- nothing that is drawn or measured, but not the same field.  So the boxes are grown by the band)
            cell = 1.5 * spacing
            boxes.extend(_cover_boxes(points, cell, max(1, int(math.ceil((max(reach, width, 0.6 * most) + 2.0 * ctx.step) / cell))), 96))
    for rule in ('min_wall', 'min_gap'):
        r = before.report.get(rule)
        if r:
            pad = 1.2 * float(r['limit'])
            for g in r['regions']:
                lo, hi = g['box']
                boxes.append((tuple(c - pad for c in lo), tuple(c + pad for c in hi)))
    if not boxes:
        return None
    whole = 1.0
    for a in range(3):
        whole *= max(ctx.hi[a] - ctx.lo[a], 1e-9)
    held = 0.0
    for lo, hi in boxes:
        v = 1.0
        for a in range(3):
            v *= max(0.0, min(hi[a], ctx.hi[a]) - max(lo[a], ctx.lo[a]))
        held += v
    if held > 0.4 * whole:
        return None
    x, y, z = _xyz()
    union = None
    for lo, hi in boxes:
        b = (lo[0] - x).max(x - hi[0]).max(lo[1] - y).max(y - hi[1]).max(lo[2] - z).max(z - hi[2])          # (above 0 outside the box, a max of six planes: no square root, intervals that are tight)
        union = b if union is None else union.min(b)
    out = union * -100.0
    out._boxes = len(boxes)
    out._box_list = boxes
    return out


_SCOPE = os.environ.get('FIELDES_SCOPE', '') != '0'          # (0: the remedies are not scoped to the places they act in -- for comparing: see _scope_field)
_LAZY_UNION = True            # (False: the union of the remedies works the earlier ones out at every point of a later shell -- the way it was; for comparing)


def _refined_change(ctx, field, where, before_values, vals, each, count=30000):
    ''' (added, removed) in mm^3 of what a remedy that can reach anywhere changes, measured in two steps.  The points over the whole box (`where`, each standing for `each` mm^3) say
        WHERE it changes anything: a gusset or a teardrop of a hundred mm^3 in a box of hundreds of thousands is a few of them, and a volume counted on a few points is off by a third.
        Then a box round each of those places is measured on its share of `count` points -- the same points in a box that holds little but the change.  Places that are no more than
        a few points apart are one place; of more than eight the smallest are left at what the first points counted.  None where the first points found no change '''
    spacing = each ** (1.0 / 3.0)
    cell = 2.5 * spacing
    hits = [(p, a > 0 >= b) for p, a, b in zip(where, before_values, vals) if (a > 0 >= b) or (a <= 0 < b)]
    if not hits:
        return None

    def key(p):
        return (int(math.floor(p[0] / cell)), int(math.floor(p[1] / cell)), int(math.floor(p[2] / cell)))

    index = {}
    for i, (p, _) in enumerate(hits):
        index.setdefault(key(p), []).append(i)
    seen, places = set(), []
    for i in range(len(hits)):
        if i in seen:
            continue
        seen.add(i)
        stack, members = [i], []
        while stack:
            j = stack.pop()
            members.append(hits[j])
            k = key(hits[j][0])
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for m in index.get((k[0] + dx, k[1] + dy, k[2] + dz), ()):
                            if m not in seen:
                                seen.add(m)
                                stack.append(m)
        places.append(members)
    places.sort(key=len, reverse=True)
    added = removed = 0.0
    for members in places[8:]:
        added += each * sum(1 for _, was_added in members if was_added)
        removed += each * sum(1 for _, was_added in members if not was_added)
    places = places[:8]
    per = max(2000, count // len(places))
    for members in places:
        lo = [min(p[i] for p, _ in members) - 1.5 * spacing for i in range(3)]
        hi = [max(p[i] for p, _ in members) + 1.5 * spacing for i in range(3)]
        volume = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])
        pts = [(lo[0] + u * (hi[0] - lo[0]), lo[1] + v * (hi[1] - lo[1]), lo[2] + w * (hi[2] - lo[2])) for u, v, w in _halton(per, (0.37, 0.61, 0.23))]
        a0 = _values(ctx.field, pts)
        a1 = _values(field, pts)
        share = volume / float(per)
        added += share * sum(1 for a, b in zip(a0, a1) if a > 0 >= b)
        removed += share * sum(1 for a, b in zip(a0, a1) if a <= 0 < b)
    return added, removed


class ManufacturingResult(FieldResult):
    ''' The part the shell made, and what it did.  It is a field like the part is -- wherever a body goes it goes (`offset(result, 1)`, `difference`,
        the part of an analysis, an export).  Stated on its own it is shown coloured by the geometry change: red where material was added, blue where it was
        removed, the part's colour where it is unchanged.

            part          the part as it was
            changes       what each remedy did: {remedy, rule, added, removed (mm^3), kept, why}
            added, removed   mm^3 of material added and cut in all (measured on points of space: about, to a few per cent)
            share         the part's volume changed, as a share
            before        the check of the part as it was (a ManufacturingAnalysis)
            after         the check of what the shell made, when it was made (a ManufacturingAnalysis), or None
            fail_states   what is still broken, where: {rule, where, share, why} -- the five biggest places of each rule
            fail_totals   all the places the re-check found, of each rule: {rule: {places, share}} (what the card counts; the shares are shares of the surface)
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

    _show_turned = True

    def _turning(self):
        ''' Whether the part is DRAWN standing the way the print is made: the tool found that way (the constraint left it free) and the script did not ask for the part as it was
            (`show_turned=False`).  The part in the model, `.part`, is not turned either way; `.oriented()` is the turned part '''
        return self._show_turned and self.orientation is not None

    def _turn(self, shape):
        ''' What is drawn of a field of the part -- the part, the change, the supports -- turned as `oriented()` turns the part, when it is drawn turned '''
        return self.oriented(shape) if self._turning() else shape

    def _turned_box(self, box):
        ''' The box of a shape (low corner, high corner) as it is once turned: the box of its eight corners turned the way `oriented()` turns the part (about z by -phi, then about y by -theta) '''
        if not box or not self._turning():
            return box
        d = self.build_direction
        phi = math.atan2(d[1], d[0]) if (abs(d[0]) > 1e-12 or abs(d[1]) > 1e-12) else 0.0
        theta = math.acos(max(-1.0, min(1.0, d[2])))
        cz, sz = math.cos(-phi), math.sin(-phi)
        cy, sy = math.cos(-theta), math.sin(-theta)
        lo, hi = box
        corners = []
        for x in (lo[0], hi[0]):
            for y in (lo[1], hi[1]):
                for z in (lo[2], hi[2]):
                    x1, y1 = cz * x - sz * y, sz * x + cz * y
                    corners.append((cy * x1 + sy * z, y1, -sy * x1 + cy * z))
        return (tuple(min(c[i] for c in corners) for i in range(3)), tuple(max(c[i] for c in corners) for i in range(3)))

    def _places(self):
        ''' how many places the re-check found in all (`fail_states` lists the five biggest of each rule) '''
        totals = getattr(self, 'fail_totals', None)
        return sum(t['places'] for t in totals.values()) if totals else len(self.fail_states)

    def _places_share(self):
        ''' the share of the surface they are, all of them '''
        totals = getattr(self, 'fail_totals', None)
        return sum(t['share'] for t in totals.values()) if totals else sum(s['share'] for s in self.fail_states)

    def _display(self):
        if self._shown is None:
            note = (_tr('Red and blue show what was added and removed since the first part. The numbers are those of this pass.') if getattr(self, '_chained', False)
                    else _tr('Red and blue show what was added and removed compared with the input part.'))
            if any(c['kept'] and c['remedy'] in ('corners', 'access') and c['added'] > 0 for c in self.changes):
                # (a hole or a slot that the ball cannot enter is closed by the rounding, as a gap is: said on the card, with what to do about it -- a hole is the design, and the user's to protect)
                note += ' ' + _tr("A hole or a slot narrower than the cutter's ball is filled up to the arc the ball rests on. To leave one as it is, exclude it: exclude(part, region), or give the region to keep=.")
            rows = [(_tr('Material added'), _quantity(self.added, 3), 'mm³'), (_tr('Material removed'), _quantity(self.removed, 3), 'mm³'),
                    (_tr('Volume changed'), _quantity(100.0 * self.share, 3), _tr('% of the part')),
                    # (how much of the surface the places are, beside their number: four places of 0.08 % each read very differently from four places of a tenth of the part.  All the places the re-check
                    # found, and not only the five biggest of each rule that are listed: the card said 1.9 % of the surface where it found 6.9 %)
                    (_tr('Places still breaking a rule'), '%d' % self._places(),
                     ('(%s %s)' % (_quantity(100.0 * self._places_share(), 2), _tr('% of the surface'))) if self.fail_states else '')]
            # (what each remedy did that was kept, by its own name -- "Draft applied to walls" is why a wall is red along its foot: without these rows the card said that material was added, and not what for)
            rows[3:3] = [(_REMEDY_TEXT[c['remedy']], _quantity(c['added'] + c['removed'], 3), 'mm³') for c in self.changes if c['kept']]
            if self.orientation is not None:
                # (the way up of the print was left free, and the tool found it: it is on the card, and `.oriented()` is the part turned to stand that way)
                rows.append((_tr('Way up of the print'), '(%.2g, %.2g, %.2g)' % tuple(self.build_direction), _tr('part shown turned') if self._turning() else ''))
            # (the result is drawn as ONE body, the surface of the result, coloured by the change at each point of it: red where it lies outside the input part, blue where inside it, the part's colour on the old surface.
            # It used to be three bodies -- the part where it is as it was, the material added in red, the material cut in blue -- and the red body of a fillet or a gusset is a sliver that thins to nothing along
            # its edges, which the mesher cannot hold under one cell: saw teeth along both edges of the ring round a boss; and a second pass over a result had its first pass's material "as it was", grey.
            # The colour is a class read as the difference of two fields, and the viewer cuts the triangles where it changes, so that the edge of the red is as fine as the smallest triangles)
            # (what the tool did bounds what can be shown: a result that only added material has cut nothing, whatever a vertex a fraction of a cell off the surface reads -- on the rough surface of an optimization
            # a layer of a tenth of a millimetre drawn in cells of a millimetre had specks of blue over all of it, beside a card that said 0 mm3 removed -- and one that only cut has added nothing)
            # (what is drawn is the preview when the tool made one -- the correction lattice of the walls, the gaps and the draft, a quarter of a millimetre between nodes, read in one lookup -- and the result
            # itself, the formula, for every other part.  The formula is what every number, check, export and further operation reads; it draws in a minute where the lattice draws in a second)
            drawn = getattr(self, '_preview', None)
            change = getattr(self, '_preview_change', None) if drawn is not None else None
            if drawn is not None:
                body_note = ' ' + _tr('What is drawn is a lattice copy of the result (%.2g mm between its nodes); the numbers, the checks and the exports are those of the exact result.') % self._preview_pitch
            else:
                body_note = ''
            if drawn is None:
                drawn, change = self._field, self.change_map
            if not getattr(self, '_cuts', True):
                change = change.max(0)
            if not getattr(self, '_adds', True):
                change = change.min(0)
            change = self._turn(change)             # (drawn standing the way the print is made, when the way up was left free: the colours turn with the part)
            body = colored(self._turn(drawn), change, range=(-1.0, 1.0), label=_tr('Geometry change'), colormap='change')
            body._color_fields = [('change', _tr('Geometry change'), change, -1.0, 1.0)]
            body._color_field_name = 'change'
            body._value_note = note + body_note
            body._key_results = rows
            box = self._turned_box(getattr(drawn, '_bounds', None))
            if box and getattr(body, '_bounds', None) is None:
                body._bounds = box
            shown = [body]
            if getattr(self, 'supports', None) is not None:
                # (ONE class, amber, and a card with one swatch that says what it is: a scale from -1 to 1 for a body of one colour said nothing)
                supports = colored(self._turn(self.supports), Shape.wrap(1.0), range=(-1.0, 1.0), label=_tr('Supports'), colormap='support')
                sbox = self._turned_box(getattr(self.supports, '_bounds', None))
                if sbox and getattr(supports, '_bounds', None) is None:
                    supports._bounds = sbox
                shown.append(supports)
            self._shown = shown if len(shown) > 1 else shown[0]
        return self._shown

    def __repr__(self):
        lines = [_tr('Manufacturing optimization: %s mm³ added, %s mm³ removed (%s %% of part volume), %.1f s') % (
            _quantity(self.added, 4), _quantity(self.removed, 4), _quantity(100.0 * self.share, 2), self.seconds)]
        for c in self.changes:
            word = _REMEDY_TEXT[c['remedy']]
            if c['kept']:
                lines.append(_tr('    %s: %s mm³ added, %s mm³ removed') % (word, _quantity(c['added'], 4), _quantity(c['removed'], 4)))
            else:
                lines.append(_tr('    %s: not applied -- %s') % (word, c['why']))
        if self.orientation is not None:
            lines.append(_tr('  Build direction found: (%.3g, %.3g, %.3g)') % tuple(self.build_direction))
        if self.fail_states:
            lines.append(_tr('  Rule violations remaining:'))
            for s in self.fail_states[:10]:
                lines.append(_tr('    %s: %s, %.2g %% of surface area -- %s') % (_RULE_TEXT[s['rule']][0], _where(s['where']), 100.0 * s['share'], s['why']))
            # (the places a rule has beyond the five listed: how many, and how much of the surface -- the list is what is biggest, not all there is)
            for rule, t in (getattr(self, 'fail_totals', None) or {}).items():
                listed = [s for s in self.fail_states if s['rule'] == rule]
                more, share = t['places'] - len(listed), t['share'] - sum(s['share'] for s in listed)
                if more > 0 and share > 1e-6:
                    lines.append(_tr('    %s: %d more places, %.2g %% of surface area, not listed above') % (_RULE_TEXT[rule][0], more, 100.0 * share))
        elif self.after is not None:
            lines.append(_tr('  Re-check of the result: all rules satisfied'))
        for note in self.notes:
            lines.append('  ' + note)
        lines.append(_tr('  The optimizer\'s own re-check, not a substitute for an independent one: run manufacturing_analysis on the result.'))
        return '\n'.join(lines)


@_with_conditions
@_remembered('manufacturing_optimization')
@_with_bar('manufacturing_optimization')
def manufacturing_optimization(part, constraints, keep=None, remedies=None, max_change=0.25, max_support_height=None, verify=False, samples=2500, show_turned=True):
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
            verify       False (the default): the shell does not check what it made -- run manufacturing_analysis on the result, which is the check.  True: the shell
                         checks its result with the same rules and says what is still broken and where (`fail_states`, and the card's "places still breaking a
                         rule"); that is the whole of the check run again on the result, and a part whose field is slow to evaluate pays for it many times over
                         (on a topology-optimized bracket it was ten of the thirteen minutes)
            samples      how many points of the surface it checks
            show_turned  True (the default): when the constraint left the way up of the print free and the tool found it, the result is DRAWN standing that way (z up) -- the
                         part turns on the screen, the card says "Way up of the print ... part shown turned"; False: it is drawn where it is.  The model is not turned either
                         way: `.oriented()` is the part turned to stand that way, for a print

        Returns a ManufacturingResult: a body that is the part changed, coloured by the geometry change, with `.changes`, `.added`, `.removed`, `.fail_states`.
        Run `manufacturing_analysis` on the result: the shell's own check is not a substitute for the maker's. '''
    t0 = time.time()
    items = _constraints_given(constraints, 'manufacturing_optimization')
    shape = Shape.wrap(part)
    ctx = _Context(shape)
    # (a part that is slow to evaluate is not checked again by the shell)
    probe_pts = [(ctx.lo[0] + (ctx.hi[0] - ctx.lo[0]) * (i + 0.5) / 100.0, ctx.lo[1] + 0.3 * (ctx.hi[1] - ctx.lo[1]), ctx.lo[2] + 0.5 * (ctx.hi[2] - ctx.lo[2]))
                 for i in range(100)]
    slow = _us_per_point(ctx.field, probe_pts, 3)[0] * len(probe_pts) > 0.25e6          # (100 points take more than a quarter of a second; the fastest of three runs, not a single one)
    notes = []
    _progress(0.02, _tr('Reading part'))
    if getattr(ctx, '_from_result', False):
        _log(t0, 'the part is a result of the tool, so the offsets of the calls into it are held')
    else:
        _log(t0, 'the part is read: a point of its field costs %.1f us, so the offsets of the calls into it are %s' % (ctx.point_us, 'held (it is expensive)' if ctx.clamp_calls else 'left as they are'))
    keeps = [r for r in (list(keep) if isinstance(keep, (list, tuple)) else ([keep] if keep is not None else []))]
    for _, region in locks_of(shape):
        keeps.append(region)
    with _within(0.05, 0.19):                 # (the check of the part has this stretch of the bar, and says how far it is within it)
        # (the check of the part as it is: its way up, its points, what breaks -- outside the regions that are kept: nothing may be changed there, so what they break is no business of the remedies)
        before = _analyse(shape, items, int(samples), what='manufacturing_optimization', progress=True, kept=keeps)
    if before is None:
        raise ManufacturingError('manufacturing_optimization: the part has no surface to measure (is it empty?)')
    _log(t0, 'sampled the surface of the part: %d points' % len(before.points))
    direction = before.orientation
    free = any(c.free_direction for c in items)
    allowed = set(remedies) if remedies is not None else set(REMEDIES)
    unknown = allowed - set(REMEDIES)
    if unknown:
        raise ManufacturingError('manufacturing_optimization: no remedy %s -- the remedies are %s' % (', '.join(sorted(unknown)), ', '.join(REMEDIES)))
    eps = _feather(ctx)
    keep_weight = None
    for r in keeps:
        w = _weight(Shape.wrap(r), eps)            # (1 inside: the part stays as it is there)
        keep_weight = w if keep_weight is None else keep_weight.max(w)
    # (what a kept region does to material that is ADDED -- a gusset, a fill -- is cut at its surface, eased over a tenth of a millimetre and not over the feather above, which is a hundredth of the part: 2.5 mm on the bracket.
    # That feather reaches that far OUT of the region, and a gusset under a kept plate lost everything within 2.5 mm of it, so that the rest stood off the plate with a gap of air between: four pieces of
    # added material floating at the height of the plates' underside, 62 to 12 triangles each, on the printer's result of the topology bracket.  Material added next to a kept part changes nothing of the part)
    keep_fill = None
    for r in keeps:
        w = _weight(Shape.wrap(r), 0.4 * ctx.step)
        keep_fill = w if keep_fill is None else keep_fill.max(w)
    keep_field = None                # (the kept regions as one field, negative inside them: the overhang's gusset is made knowing where they are)
    for r in keeps:
        w = Shape.wrap(r)
        keep_field = w if keep_field is None else keep_field.min(w)
    reach = float(max_support_height) if max_support_height else 0.2 * ctx.size
    pts, box = _volume_points(ctx, 12000)
    f0_vals = _values(ctx.field, pts)
    inside0 = [v <= 0 for v in f0_vals]
    volume = box * sum(inside0) / float(len(pts))
    if not volume > 0:
        raise ManufacturingError('manufacturing_optimization: the part has no volume to measure (is it empty, or very thin?)')
    budget = max_change * volume
    _progress(0.2, _tr('Measuring part volume'))
    _log(t0, 'the volume of the part is measured')

    # The remedies, in the order they are made.  A part that is expensive to read has the lattices of the kernel for its remedies: native fields, made once, that cost next to nothing to read again.  Each is then
    # made from the RESULT of the ones before it, so that the new faces one makes -- the walls of an undercut fill, the edge of a draft skin, the flank of a gusset, a web left between two fills -- are seen by the
    # ones that judge faces after it (the corners are rounded last, so the inside corners that the fills made are rounded too; the walls after the fills, so a sliver they left is thickened).  A small part keeps
    # the formulas, which are exact and cost a multiple of themselves at every step of a chain: each remedy is made from the part as it is, and they are united
    chained = bool(ctx.clamp_calls)
    inexact = chained or ctx.slope_spread > 0.02          # (a part that is expensive to read, or whose field is not a distance: what the check reads of it is good to a few per cent, and the draft and the corners are made that much over what was asked)
    steps = []                       # (remedy, rule, value, constraint, the way up, the weight of the constraint's region)
    for c in items:
        d = direction if c.build_direction is None else c.build_direction
        w_region = None if c.region is None else _weight(c.region, eps)
        for rule, value in c.rules.items():
            remedy = _RULE_REMEDY.get(rule)
            if remedy is not None and remedy in allowed:
                steps.append((remedy, rule, value, c, d, w_region))
    # (a gap that the rounding of the inside corners closes is not closed a second time: a ball of radius r cannot enter a gap narrower than 2 r, and the rounding -- a ball rolled along the part -- fills it up to
    # the arc that the ball rests on.  A fill of the gap on top of that, made from the part as it was and united, rises flat to the top of the walls with a hair to spare -- 2 % of the limit, 0.12 mm of a gap of 6 --
    # and stands past their ends and above their tops with new inside corners, sharp ones, that nothing rounds.  A milling cutter's rules are such a pair: its radius is half its diameter, which is the gap)
    covered = []
    for s in list(steps):
        if s[0] == 'gaps' and s[3].gap_fix == 'fill' and 'corners' in allowed:
            radius = s[3].rules.get('min_radius')
            if radius and float(radius) >= 0.5 * float(s[2]) * (1.0 - GRACE) and before.report.get('min_radius', {}).get('broken', 1) > 0:
                steps.remove(s)
                covered.append(s)
    again_from = None                # (the index of the first undercut fill that is a repeat: see below)
    if chained:
        steps.sort(key=lambda s: _CHAIN_ORDER.index(s[0]))
        # (an undercut rule and a rule for the radius of the inside corners, asked together, are ONE remedy on such a part: the air that a ball-end tool of that radius cannot touch along the axis is filled -- see
        # _remedy_access --, and it goes last, after the walls, because what it leaves is what the tool reaches.  Made one after the other the two undid each other: the fill's walls are sharp corners, the fillets'
        # webs cap columns of air)
        under = [s for s in steps if s[0] == 'undercut']
        rounds = [s for s in steps if s[0] == 'corners']
        if under and rounds:
            u, r = under[0], rounds[0]
            steps.remove(u)
            steps.remove(r)
            steps.append(('access', u[1], (u[2], r[2], 0.0), u[3], u[4], u[5]))
            # (and the draft goes after it, last: the fill's walls are vertical and the draft is theirs to give.  Asked of the operator itself, as the taper of the tool's shaft, the draft was missing on 57 % of
            # the walls of the bracket's mould, where the draft remedy leaves it on 19 %: a lean of one degree over a wall of 10 mm is a skin of 0.19 mm, under half a node of the lattice, and the noise of the
            # distances on it -- a few hundredths of a millimetre -- is the lean.  The draft remedy works it out from the part's smooth normals, exact on a lattice of that size)
            drafts = [s for s in steps if s[0] == 'draft']
            for dr in drafts:
                steps.remove(dr)
            steps.extend(drafts)
        # (what the remedies after the undercut fill add -- the fillets of the corners, a wall thickened, a gusset -- can cap a column of air that the fill saw open: a fillet in a crevice with a lid over it, and the
        # column has material above and below again.  The fill is made once more, last, from the result, when something was kept after the first; its entry is merged into the first one's)
        fills = [s for s in steps if s[0] == 'undercut']
        if fills and steps[-1][0] != 'undercut':
            again_from = len(steps)
            steps.extend(fills)
    first_fill = next((i for i, s in enumerate(steps) if s[0] == 'undercut'), None)
    kept_since_fill = 0
    changes = []
    for s in covered:
        changes.append({'remedy': 'gaps', 'rule': s[1], 'added': 0.0, 'removed': 0.0, 'kept': False,
                        'why': _tr('closed by the rounding of the inside corners: a ball of %.2g mm radius cannot enter a gap narrower than %.2g mm') % (float(s[3].rules['min_radius']), 2.0 * float(s[3].rules['min_radius']))})
    adds, cuts = [], []
    offsets, unions = [], []         # (the remedies that move the surface -- walls, draft, a gap filled -- are ADDED to each other as growths: (growth, band width, most) each; the others are united: `unions`)
    acting = []                      # (for each of those: the points it was measured on where it moves the surface, the spacing of them, the most it moves it -- the places it acts in, see `_scope_field`)
    feet = []                        # (the pull directions of the drafts that were kept, and the way up for the walls that were thickened: the feet of the walls that grew are closed along them, see `_close_feet`)
    measured = []                    # (entry, field, the points it was measured on, the part's values there, the volume of a point, a shell?, the remedy's own values there) of each kept additive remedy
    spent = 0.0
    shells = {}
    current, cx = None, ctx          # (chained: the result so far, and the context it is read in; None: the part itself)
    current_vals = f0_vals
    for index, (remedy, rule, value, c, d, w_region) in enumerate(steps):
        if again_from is not None and index >= again_from and not kept_since_fill:
            continue                 # (the fill made again has nothing new to look at: no remedy kept anything since the first)
        stretch = (0.19 + 0.56 * index / max(1, len(steps)), 0.19 + 0.56 * (index + 1) / max(1, len(steps)))          # (this remedy's part of the bar)
        what = _tr('Measuring effect: %s') % _REMEDY_TEXT[remedy]
        _progress(stretch[0], what)

        # (a kept region is cut out of what a remedy makes, and how far out depends on what the remedy makes: material that FILLS -- a gusset, the rounding of a corner, an undercut filled -- is cut at the surface of
        # the region, eased over a tenth of a millimetre (`keep_fill`), so that it reaches the kept part and does not float beside it; a SKIN grown off a face -- the draft, a wall made thicker, a gap closed -- is
        # eased out over the feather round the region (`keep_weight`, a hundredth of the part), because the skin of a kept face reaches out of the region by up to the most the remedy moves it.  Cut sharp at
        # the surface of a kept box that is a millimetre wider than the plate in it, the draft's skin stood off the plate as a sheet of 0.6 mm with air between: 26 thin walls of 581 on the mould of the bracket)
        def scoped(new, w_region=w_region, cx=cx, skin=remedy in ('walls', 'gaps', 'draft')):
            out = new
            if w_region is not None:
                out = cx.base + w_region * (out - cx.base)
            keep_here = keep_weight if skin else keep_fill
            if keep_here is not None:
                out = out + keep_here * (cx.base - out)
            return out
        field, additive, width, offset = None, True, None, None
        if remedy == 'walls':
            # (grown to the limit and 4 % over it: the check reads a wall also along lines leaning 20 degrees off its normal, which on a sheet that curves tightly -- a gyroid's, a radius of 2 mm to a wall of 1.6 --
            # read a few per cent less than the wall along the normal, and a wall grown to exactly the limit was marked there; the wall that comes out is at most 6 % over what was asked)
            field, offset = _as_offset(cx, _remedy_walls(cx, value * 1.04, threshold=value, growth=True), w_region, keep_weight, scoped, not chained)
            width = value
        elif remedy == 'gaps':
            field, offset = _as_offset(cx, _remedy_gaps(cx, value, c.gap_fix, growth=True), w_region, keep_weight, scoped, not chained)
            additive, width = c.gap_fix == 'fill', value
        elif remedy == 'corners':
            # (the library's fillet is read from the part's field by Newton steps along its gradient, good to a few tenths of a per cent: over a smooth part that has no corner to round it is the part's
            # own field to that much and can add specks where the walk falls short -- with the mesh it replaced a torus came out with 3.9 mm^3 "added" and a few red dots --, and it is a fair
            # part of the cost of the field.  So the remedy is made where the check of the part finds a corner short of the radius at one of its samples, and not where it finds none -- or where an
            # earlier remedy has made new faces, which the check of the part did not see; the other remedies are exact fields, which are the part's own where nothing is wrong, and are made as ever)
            width = value
            if before.report.get(rule, {}).get('broken', 1) == 0 and current is None:
                field = None
            else:
                # (a ball of the radius finds nothing to fill where the air is more than a radius from the part.  The corners are rounded to the radius and 8 % over it: what the check reads of a corner is the clearance of a
                # ball's centre from probes along the normal and on a sphere, good to a few per cent, and a surface made to exactly the limit is marked at one sample in two by that noise -- as the walls are grown 4 % past
                # theirs and the overhang's slope is held 5 % inside)
                field = scoped(_remedy_corners(cx, value * (1.08 if inexact else 1.0)))
        elif remedy == 'access':
            axis_, radius_, taper_ = value
            # (made when the check of the part finds an undercut, a corner short of the radius or a wall short of the draft; the radius is the one asked, and 8 % over it for a part whose field the check reads to a few per cent)
            if (before.report.get('undercut_along', {}).get('broken', 1) == 0 and before.report.get('min_radius', {}).get('broken', 1) == 0
                    and (not taper_ or before.report.get('draft', {}).get('broken', 1) == 0) and current is None):
                field = None
            else:
                made = _remedy_access(cx, axis_, float(radius_) * (1.08 if inexact else 1.0), taper_, True)
                field = scoped(made) if made is not None else None
            width = None
        elif remedy == 'undercut':
            field = scoped(_remedy_undercut(cx, value))
        elif remedy == 'draft':
            # (the growth is at most the part's height along the pull times the tangent of the draft, and lies in a band that wide round the surface: measured on points of that shell, like the layers of
            # the walls and the gaps.  Measured on twelve thousand points over the whole box it was read to 5 %, and the card said 1,321 mm^3 of a change of 1,180 on the moulding sampler)
            # (the walls lean the draft and 8 % more: the check reads a lean from normals good to a degree or so, and a wall made to exactly the draft asked is marked at one sample in two by that noise)
            # (and 3 % over for a part with exact fields, as the walls are grown 4 % over: the independent check reads the lean of a face from the gradient at a sample that lies a few hundredths of a millimetre
            # off it, and reads a face of 1.08 degrees as 0.86 there -- a draft made to exactly the angle asked sits on the edge of its tolerance, 0.95, and d5's third pass was marked on 0.45 % of the surface)
            lean = value * (1.08 if inexact else 1.03)
            field, offset = _as_offset(cx, _remedy_draft(cx, lean, c.pull_direction, growth=True), w_region, keep_weight, scoped, not chained)
            width = _draft_reach(cx, lean, c.pull_direction)[1]
        elif remedy == 'overhang':
            made = _remedy_overhang(cx, value, d, c.overhang_fix, reach, keep_field)
            info = made._lattice
            _log(t0, 'overhang field made on a lattice of %d x %d x %d nodes (%.3g mm apart) in %.1f s, %.1f s of it sampling the part at %d nodes: %.4g mm^3' % (
                info['dims'][0], info['dims'][1], info['dims'][2], info['pitch'], info['seconds'], info['sampling_seconds'], info['looked_at'], info['volume']))
            field, additive = scoped(made), c.overhang_fix == 'fill'
        if field is None:                    # (a remedy that was not made: the check finds nothing for it to do)
            changes.append({'remedy': remedy, 'rule': rule, 'added': 0.0, 'removed': 0.0, 'kept': False, 'why': _tr('no change required')})
            continue
        _progress(stretch[0] + 0.5 * (stretch[1] - stretch[0]), what)
        # (what a remedy of a known width changes lies in the shell of that width round the surface: measured on points of the shell, so that a thin layer or a narrow gap has
        # points in it; one that can reach anywhere on points over the whole box.  What it changes is measured against the result so far)
        shell = None
        if width is not None:
            if width not in shells:
                shells[width] = _shell_points(ctx, width)
            shell = shells[width]
        if shell is not None:
            where, before_values, each = shell
            if current is not None:
                before_values = _values(current, where)
        else:
            where, before_values, each = pts, current_vals, box / float(len(pts))
        _progress(stretch[0] + 0.7 * (stretch[1] - stretch[0]), what)            # (the points it is measured on are made: the remedy's field is worked out on them next)
        vals = _values(field, where)
        _progress(stretch[0] + 0.95 * (stretch[1] - stretch[0]), what)
        added = each * sum(1 for a, b in zip(before_values, vals) if a > 0 >= b)
        removed = each * sum(1 for a, b in zip(before_values, vals) if a <= 0 < b)
        if shell is None:
            refined = _refined_change(cx, field, where, before_values, vals, each)
            if refined is not None:
                added, removed = refined
        entry = {'remedy': remedy, 'rule': rule, 'added': added, 'removed': removed, 'kept': False, 'why': ''}
        _log(t0, 'remedy %s measured: +%.4g -%.4g mm^3' % (remedy, added, removed))
        if added + removed < 1e-4 * volume:
            entry['why'] = _tr('no change required')
            if remedy == 'corners' and getattr(cx, 'corners_left', None) is not None:
                entry['why'] = _tr('not rounded: a ball of %.2g mm is smaller than this part\'s lattice resolves') % cx.corners_left
        elif spent + added + removed > budget:
            entry['why'] = _tr('would change %.2g %% of the part volume; max_change allows %.2g %%: add a support or change the design') % (
                100.0 * (spent + added + removed) / volume, 100.0 * max_change)
        else:
            entry['kept'] = True
            if first_fill is not None and first_fill < index and (again_from is None or index < again_from):
                kept_since_fill += 1
            spent += added + removed
            (adds if additive else cuts).append(field)
            if additive and offset is not None:
                offsets.append(offset)
                if shell is not None:
                    moved = 1e-7 * ctx.size
                    acting.append(([p for p, a_, b_ in zip(where, before_values, vals) if a_ - b_ > moved], each ** (1.0 / 3.0), float(offset[2]), float(offset[1])))
                if remedy == 'draft':
                    feet.insert(0, c.pull_direction)           # (the draft's own pull first: it is the direction the feet are closed along when there is one)
                elif remedy == 'walls':
                    feet.append(d)                              # (a thickened wall has a foot as a drafted one does: the wedge of air where the floor it stands on is nearer than the wall; closed along the way up)
            elif additive:
                unions.append(field)
            if additive:
                measured.append((entry, field, where, before_values, each, shell is not None, vals))
            if chained:
                # (the next remedy is made from what this one made: the field is the part united with what the remedies so far made -- every remedy's field is the field it was made in, changed)
                current = field
                current._bounds = (tuple(ctx.lo[i] - 0.05 * ctx.size for i in range(3)), tuple(ctx.hi[i] + 0.05 * ctx.size for i in range(3)))
                current._plate_box = ctx.plate_box
                current_vals = _values(current, pts)
                cx = _Context(current)
                # (a chain is only made of a part that is expensive to read, and what the next remedy reads is the part united with lattices that are cheap: measured again it would look cheap, and the next remedy would be
                # asked as a formula of the whole chain -- the mould's corners on the optimized bracket did not end in eight minutes.  The chain is as expensive as its part was)
                cx.clamp_calls = True
        changes.append(entry)
    if again_from is not None:
        # (the fill made again is one row with the first: what the two added is what the undercut rule cost)
        merged, seen = [], {}
        for e in changes:
            first = seen.get((e['remedy'], e['rule']))
            if first is None:
                seen[(e['remedy'], e['rule'])] = e
                merged.append(e)
            elif e['kept']:
                first['added'] += e['added']
                first['removed'] += e['removed']
                first['kept'], first['why'] = True, ''
        changes = merged
    # (what the result adds is the volume of the UNION of what the kept additive remedies add, not the sum of what each adds alone: where two overlap -- a draft wedge and an undercut fill on one wall, a
    # fillet at the root of a thickened wall -- the sum counts the place twice, and the card said 1516 mm3 of a change of 1180.  Each remedy after the first is measured for what it adds that the ones before it
    # did not, on the points it was measured on)
    union_added = None
    if len(measured) > 1 and not chained:
        union_added, earlier = 0.0, None
        for entry, field, where, before_values, each, is_shell, own in measured:
            if earlier is None:
                marginal = entry['added']
            elif is_shell and _LAZY_UNION:
                # (`beyond` below is in the part where the part is, and where this remedy is and the ones before it are not: so only the points this remedy adds -- known from its own values, measured
                # above -- can count, and of those the ones where the ones before it add nothing.  The earlier remedies are worked out on those points alone, not on every point of the shell)
                added_here = [i for i, (a, b) in enumerate(zip(before_values, own)) if a > 0 >= b]
                there = _values(earlier, [where[i] for i in added_here]) if added_here else []
                marginal = each * sum(1 for g in there if g >= 0)
            else:
                beyond = ctx.field.min(field.max(-earlier))            # (the part, and what this remedy adds where the ones before it add nothing)
                vals = _values(beyond, where)
                marginal = each * sum(1 for a, b in zip(before_values, vals) if a > 0 >= b)
                if not is_shell:
                    refined = _refined_change(ctx, beyond, where, before_values, vals, each)
                    if refined is not None:
                        marginal = refined[0]
            union_added += marginal
            earlier = field if earlier is None else earlier.min(field)
    result = ctx.base
    preview, preview_pitch = None, 0.0          # (the field the window draws when it is not the result itself: see the correction lattice below, and the pitch of its nodes)
    exact_formula, zone = None, None            # (and the formula the lattice was made of, and the zone lattice: a batch of points is worked out by the one or the other, see `_values`)
    # (a remedy is the part's own field where it does nothing: the same value as the part, over most of space -- a tie -- so each is kept a few microns apart; see _join.  A chain has no ties: each field
    # is the one before it, changed, and the last is the result)
    if chained and current is not None:
        result = current
    if offsets and not chained:
        # (the remedies that move the surface along its normal -- the walls thickened, the draft, a gap filled -- are one offset: the growths are added, so that a face one of them makes -- the thickened fin's -- is
        # moved by the others -- given its draft --, where united they were each made from the part as it was and the faces of one were not seen by the others)
        total, widest, most = offsets[0][0], offsets[0][1], offsets[0][2]
        for g_, w_, m_ in offsets[1:]:
            total = total + g_
            widest = max(widest, w_)
            most = most + m_
        # (the band that holds the sum is as wide as the sum needs: the ramp of `_near` rises from 0 at the edge of the band by `10 most / width` for every unit toward the surface, and must stay above the growth
        # at the surface, which is as much as `most` -- a fin thickened by 0.53 and given 0.42 of draft has a growth of 0.95 at its foot, where the band of the walls alone, 0.936, left a ramp of 0.72 that cut the
        # growth: the surface followed the part's own level set there, plumb, and the draft was 0.6 degrees where the geometry asked 2)
        widest = max(widest, 1.2 * most + 2.0 * ctx.step)
        # (and the places they act in: outside the boxes round them the growth is 0, and the mesher is told so, see `_scope_field`)
        scope = _scope_field(ctx, acting, before, widest) if _SCOPE and len(acting) == len(offsets) else None
        if scope is not None:
            _log(t0, 'the remedies that move the surface are scoped to %d boxes round the places they act in' % scope._boxes)
        result = ctx.field - _near(total, ctx.field, widest, most, scope)
        # (what the walls, the gaps and the draft add to the part is looked at once and kept as a lattice, `preview`, so that drawing the result reads one lookup and not this formula at every point: see _bake.
        # A lattice reads a kink of the growth -- the top of a wall's draft, a convex edge, the foot of a wall -- over one node, a quarter of a millimetre, which is finer than the cells a picture is made in and
        # coarser than the independent check, which reads the lean of a face a few hundredths of a millimetre from such a kink: it marked the draft on 0.24 % of the mould's surface and the undercut on 0.16 %
        # where the formula has nothing.  So the lattice is what the WINDOW draws, and the result itself -- every number, check, export, offset and difference -- is the formula: exact.  The card says so.
        # And the result is told where the lattice is as good as the formula (`zone`, a second lattice: -1 within two nodes of a kink near the surface) so that a batch of points is worked out by the lattice
        # outside the zones and by the formula inside them, see `_values`, and the mesher drops the branch it does not need, see the `min` below)
        # (the feet of the grown walls are closed in the formula FIRST and the lattice is made of that: closed afterwards on the lattice, the closing -- the result looked up at shifted places, calls on the
        # tape of every cell near a foot, half a millisecond a call a cell -- made what was drawn 100 times dearer than the mill's lattices: 0.83 ms a cell against 0.011, the printer's first drawing
        # killed at 330 s with the render at 69 %, the mould's 29.6 s.  Nothing of the closing is left on the tape of the lattice)
        if feet:
            result = _close_feet(ctx, result, feet[0], most)
        baked = _bake(ctx, result, widest, most)
        if baked is not None:
            preview, zone = baked
            info = preview._lattice
            _log(t0, 'the walls, gaps and draft are one correction lattice of %d x %d x %d nodes (%.3g mm apart) made in %.1f s, %.1f s of it sampling the formula at %d nodes: it is what is drawn' % (
                info['dims'][0], info['dims'][1], info['dims'][2], info['pitch'], info['seconds'], info['sampling_seconds'], info['looked_at']))
            preview_pitch = info['pitch']
            exact_formula = result
            if zone is not None:
                # (`min(a, b)` with `a` the formula held far away outside the zones and `b` the lattice held far away inside them: the formula in the zones, the lattice elsewhere, where the two differ by
                # nothing that is measured; a mesher drops the branch it does not need in a cell, a batch of points is split by `_values`)
                result = (result + _BIG * zone.max(0)).min(preview + _BIG * (-zone).max(0))
    # (a remedy whose field is the exact distance of its own result -- the corners' lattice, `_full` -- is the result where nothing else moves the surface: united with the part's own field, as the others are, the part's
    # plain field came back wherever it was smaller than the true distance -- 2.5 at the axis of a hole where the rim is 3.0 away -- and the check read a ball that fits as 17 % short.  A remedy that is not whole
    # (or one that was scoped to a region, which is blended with the part) is united with it as ever)
    order = sorted(range(len(unions)), key=lambda i: 0 if getattr(unions[i], '_full', False) else 1) if not chained else []
    hair = 5.0 * ctx.tie

    def finish(result):
        ''' what the offsets made (or the part), united with the remedies that fill, the cuts taken off it, grown by a hair; the plate and the locks of the part carried on '''
        for i in order:
            a = unions[i]
            if result is ctx.base and not offsets and getattr(a, '_full', False):
                result = a + ctx.tie * (i + 1)
            else:
                result = result.min(a + ctx.tie * (i + 1))
        for i, c in enumerate(cuts if not chained else []):
            # (a cut remedy is the whole part with something cut away, and what it cuts is the part where it is positive: the difference takes away that region only.  Intersecting the
            # result with the cut remedy as a whole would cut away everything that the additive remedies built outside the part)
            # (the part's field less a skin of `step`: the cut then reaches a hair outside the part's old surface, where the remedy says "air" as well.  Cut exactly to that surface, the
            # cut and the part share it, and the difference between them is exactly 0 on it -- touching 0 without crossing it, a ghost surface inside the new air that the mesher draws as
            # triangles of no area and the export as edges of nobody: seen on a slot made wider, whose old faces read exactly 0.0 inside the new gap)
            # (but ONLY where the remedy cuts: away from the cut the remedy is the part's own field, and that skin -- the part less 0.09 mm, and the result held above its mirror image
            # outside the part -- lifted the growth of every wall and every gap off its old face wherever there was a cut anywhere: a sheet of air 0.09 mm thick between the old face and the grown
            # skin, detached from it (a fin of 1.2 grown to 2 read as 0.37 thick across it by the check).  `depth` is how far the remedy takes the surface in, nothing where it does nothing: the cut
            # is made where it is more than a fourth of a step in, and eased in over the next fourth)
            depth = c - ctx.field
            cutting = (depth * (1.0 / (0.25 * ctx.step)) - 1.0).max(0).min(1)
            removed = (ctx.field - ctx.step).max(-(c - 10.0 * ctx.size * (1.0 - cutting)))             # (negative where the part is cut away, and nowhere else)
            result = result.max(-removed - ctx.tie * (i + 1))
        # (the whole result grown by a hair, 5 ties -- under a tenth of a micron: where the part's faces lie exactly on the grid the picture is drawn on (integer coordinates, as scripts have
        # them), a surface that is the part's own gives exactly 0 at the grid points on it, and where a remedy's surface meets it at an edge there are several surfaces through the
        # same grid point, which the mesher settled with edges of more than two triangles and triangles of no area.  Grown by a hair no grid point is exactly on a surface)
        # (not inside a kept region: there the result is the part's own field and nothing else, exactly -- a kept plate's surface moved by the hair, 0.0006 mm on the bracket)
        result = result - (hair * (1.0 - keep_fill) if keep_fill is not None else hair)
        result._bounds = (tuple(ctx.lo[i] - 0.05 * ctx.size for i in range(3)), tuple(ctx.hi[i] + 0.05 * ctx.size for i in range(3)))
        # (the plate the print stands on is the part's box, not the result's padded one: a check or a second pass made on the result reads it from here, and the bottom face of the part, resting on
        # the plate, is no overhang -- read from the padded box it stood 5 % of the part's size above the plate and was a flat roof: 6 % to 15 % of the surface of the result "overhanging")
        result._plate_box = ctx.plate_box
        for name in ('_locks', '_free'):
            v = getattr(shape, name, None)
            if v is not None:
                setattr(result, name, v)
        return result

    result = finish(result)
    if preview is not None:
        preview = finish(preview)
        if zone is not None and exact_formula is not None:
            # (the three of them, made the same way: the result as a point is worked out by it -- the lattice outside the zones, the formula inside them -- by `_values`)
            result._hybrid = (finish(exact_formula), preview, zone)
    # (the colour is a CLASS -- added, as it was, cut -- and not an amount: a step from 0 to 1 over `eps` of the distance the result's surface stands out of the part's, or in.  It is the difference of the
    # two fields at the point, the part's less the result's: where the result is the part's own, the two are the same value at any point near the face, and the difference is nothing wherever the vertex of
    # the drawn mesh lies -- and a vertex of a mesh lies up to a half cell off the true surface, and at an edge (the rim of a hole, the corner of a block) it is off along the diagonal.  Where a layer of
    # thickness t was added, the result is the part's less t at every point across it, and the difference is t.  The colour used to be the part's field at the vertex carried along the part's normal
    # onto the result's surface: at the rim of a hole that normal is the top face's, the carried point lands in the hole, and an untouched rim read 1.0 -- the pale red that ran across the flat face from every
    # rim vertex, in the picture of the mould.  A ramp of 0.15 mm besides made a tail of a fillet, thinner than that over a millimetre of its length, a fading pink on the flat beyond it)
    eps = max(0.02, 3e-4 * ctx.size)
    delta = ctx.field - result
    change_map = ((delta - eps).max(0).min(eps) - (-delta - eps).max(0).min(eps)) * (1.0 / eps)
    tol = 2.0 * eps
    # (a result handed to the tool again is coloured by everything made since the FIRST part: the red of the earlier passes stays and what this pass adds or cuts is added to it -- a cut of material an earlier pass
    # added is back to grey.  Coloured by this pass alone, the second pass over a ledge's gusset, which changes nothing, showed the whole of the gusset grey.  The numbers on the card stay those of this pass)
    # (the decorators of the tool hand it the FIELD of a result given as the part, not the result: `_with_conditions` replaces the first argument by `_field_of(it)`, so that the colour of the earlier passes travels on
    # the field, as the plate of the print does (`_plate_box`): a test that the part "is a ManufacturingResult" was never true inside this function, and the second pass over a ledge's gusset came out grey)
    earlier = getattr(part, '_change_map', None)
    if earlier is not None:
        change_map = (change_map + earlier).max(-1).min(1)
    result._change_map = change_map
    preview_change = None
    if preview is not None:
        # (the same colour class, read from the preview: what the window draws is coloured by what it draws)
        pdelta = ctx.field - preview
        preview_change = ((pdelta - eps).max(0).min(eps) - (-pdelta - eps).max(0).min(eps)) * (1.0 / eps)
        if earlier is not None:
            preview_change = (preview_change + earlier).max(-1).min(1)
        preview._change_map = preview_change
    supports, blockers = None, []
    supporting = [c for c in items if c.support and 'overhang' in c.rules]
    support_volume = 0.0
    if supporting:
        _progress(0.78, _tr('Generating supports'))
        supports = _supports_field(ctx, result, supporting, direction, _extent_along(ctx, direction))
        svals = _values(supports, pts)
        support_volume = box * sum(1 for v in svals if v <= 0) / float(len(pts))
        for c in supporting:
            blockers += c.no_support
        _log(t0, 'supports made: %.4g mm^3' % support_volume)
    after, fails = None, []
    fail_totals = OrderedDict()            # (rule -> {places, share}: ALL the places the re-check found, of which `fails` lists the five biggest of each rule)
    _log(t0, 'the remedies are united')
    changed = bool(adds or cuts)
    if verify and not slow and (changed or supports is not None):
        _progress(0.8, _tr('Re-checking result'))
        # (the check made on the result is the check's own, manufacturing_check._analyse: nothing that made the result reads it, and an overhang with a support under it is held.  A part that
        # is left as it is and has supports under it is re-checked as well, with them: read from the check of the part alone, the overhang the support holds up was listed as broken --
        # a ledge held by a block, "1 place, 11 % of the surface" on the card)
        with _within(0.8, 0.98):
            after = _analyse(result, items, int(samples), bounds=result._bounds, plate_box=ctx.plate_box, direction=direction, seed=13, rounds=1, batch_size=20000,
                             seeds=before.points, supports=supports, progress=True, kept=keeps)
        if after is not None:
            why = _tr('still violated after the remedy') if changed else _tr('no remedy available for it')
            for rule, r in after.report.items():
                fail_totals[rule] = {'places': len(r['regions']), 'share': r['share']}
                for g in r['regions'][:5]:
                    fails.append({'rule': rule, 'where': g['at'], 'share': g['share'], 'why': why})
    elif verify and slow:
        notes.append(_tr('The part is slow to evaluate, so the result was not re-checked: run manufacturing_analysis on it'))
    elif verify and not changed:
        for rule, r in before.report.items():
            fail_totals[rule] = {'places': len(r['regions']), 'share': r['share']}
            for g in r['regions'][:5]:
                fails.append({'rule': rule, 'where': g['at'], 'share': g['share'], 'why': _tr('no remedy available for it')})
    if keep_weight is not None or any(c.region is not None for c in items):
        notes.append(_tr('Outside a constraint\'s region, and in kept regions, the part is unchanged'))
    _progress(1.0, _tr('Done'))
    _log(t0, 'done')
    if supporting:
        notes.append(_tr('Remaining overhangs are held by block supports: %s mm³, a separate body (result.supports)') % _quantity(support_volume, 4))
    out = ManufacturingResult(shape, result, change_map, changes, volume, before, after, fails, direction, free, notes, time.time() - t0)
    out._preview = preview                      # (what the window draws of the result, a lattice copy of it: None when the result itself is drawn)
    out._preview_change = preview_change
    out._preview_pitch = preview_pitch
    out._chained = earlier is not None          # (coloured by everything since the first part: the card says so)
    if union_added is not None:
        out.added = union_added
        out.share = (out.added + out.removed) / volume if volume > 0 else 0.0
    out.fail_totals = fail_totals
    out._show_turned = bool(show_turned)
    out.supports = supports
    out.support_volume = support_volume
    out.blockers = blockers
    out.change_tolerance = tol          # (mm: how far the result's surface stands out of the input surface, or in, where the change map reaches +1 or -1; it is 0 below half of that)
    # (what the tool did bounds what can be shown -- see `_display` --, and a pass over a result also shows what the passes before it did: a second pass that changes nothing still shows the first one's gusset in red)
    out._adds, out._cuts = bool(adds) or earlier is not None, bool(cuts) or earlier is not None
    return out


manufacturing_optimization._required = ('constraints',)
