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
import random
import time
from collections import OrderedDict

from fieldes.i18n import tr as _tr
from fieldes.shape import Shape
from fieldes.stdlib.fea import FieldResult, colored, _with_conditions
from fieldes.stdlib.excluded import locks_of
from fieldes.stdlib.fields import fillet as _fillet
from fieldes.stdlib.manufacturing_check import ManufacturingAnalysis, _analyse, manufacturing_analysis
from fieldes.stdlib.manufacturing_field import (_BIG, _Context, _Normals, _at, _dot, _extent_along, _frame, _join, _near, _remembered, _values, _where, _within_reach, _xyz)
from fieldes.stdlib.manufacturing_rules import (GRACE, SUPPORT_EDGE_STEPS, ManufacturingConstraint,ManufacturingError, _RULE_TEXT, _constraints_given, _log, _progress, _quantity,
                                                 manufacturing_constraint, fff_constraint, cnc_constraint, molding_constraint)

__all__ = ['manufacturing_constraint', 'fff_constraint', 'cnc_constraint', 'molding_constraint',
           'manufacturing_analysis', 'manufacturing_optimization', 'ManufacturingConstraint', 'ManufacturingAnalysis', 'ManufacturingResult']


def _on_face(ctx, tol, normals=None):
    ''' 1 where the line along the normal starts on a flat face: its foot -- the nearest point of the surface that the normal leads to, p - f n -- is on the surface (not `tol` or more
        off it: over the mouth of a slot, over the top of a fin, the line comes down in the air between two edges, or past an end) and the slope of the field there is a distance's, 1:
        at an edge or a corner the slope is the average of the two faces' and less, and a point round the corner leads to the corner, where a wall looks thinner across its diagonal
        than it is: a remedy that went by that would grow a rounded cap past the end of the wall, a lip, where it has to stop at the edge.  The slope is read at the foot by CENTRAL
        differences, which straddle an edge and read less there (forward differences read a convex edge as steeper than a face, root 2: they let every edge through) '''
    n, f = (ctx.normals if normals is None else normals), ctx.field
    px, py, pz = -f * n.x, -f * n.y, -f * n.z
    foot = _at(f, px, py, pz)
    slope = _Normals(f, ctx.step, shift=(px, py, pz)).magnitude
    on = ((tol - foot.abs()) * (1.0 / tol)).max(0).min(1)
    return on * ((slope - 0.9) * (1.0 / 0.05)).max(0).min(1)


def _chord(ctx, limit, inward, normals=None, shift=None, fshift=None, count=8):
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
    flip = 1.0 if inward else -1.0
    dx, dy, dz = (0.0, 0.0, 0.0) if shift is None else shift
    here = f if fshift is None else fshift
    previous = Shape.wrap(-1e-3 * limit)
    founds = [Shape.wrap(1.0e5)]
    for k in range(1, count + 1):
        s = reach_ * k / float(count)
        s0 = reach_ * (k - 1) / float(count)
        v = flip * _at(f, dx - (here + (-sign) * s) * n.x, dy - (here + (-sign) * s) * n.y, dz - (here + (-sign) * s) * n.z)
        a, b = previous, v
        crossing = ((a * (-1.0 / (1e-4 * limit))) + 0.5).max(0).min(1) * ((b * (1.0 / (1e-4 * limit))) + 0.5).max(0).min(1)
        frac = ((-a) / (b - a + 1e-9)).max(0).min(1)
        founds.append(s0 + (s - s0) * frac + 1.0e5 * (1.0 - crossing))
        previous = v
    return _join('min', founds, ctx.tie).min(reach_)


def _leaning_margin(ctx, limit, d, creases=False):
    ''' Above 0 where the surface faces down and leans out more than `limit` degrees from the vertical, `d` being the way up -- where a support is wanted --: the angle of the normal
        from the way up, less the limit and a degree.  Not on the first layer, which rests on the plate.  `creases=False` (what the supports are placed by): a crease, where the normal is a
        mix of two faces, is read as it is, a continuous angle -- a support under it does no harm, and a gate that shut it out put the edge of the support where the stencil of the normal
        met the part's edge, a place that moved with every height; `creases=True` leaves out where the slope of the field is less than a distance's.  The supports are placed by this, and
        not by the check of the part (manufacturing_check), which they are to be checked by '''
    n = ctx.normals
    plate = min(x * d[0] + y * d[1] + z * d[2] for x in (ctx.plate_box[0][0], ctx.plate_box[1][0]) for y in (ctx.plate_box[0][1], ctx.plate_box[1][1])
                for z in (ctx.plate_box[0][2], ctx.plate_box[1][2]))
    height = (_xyz()[0] * d[0] + _xyz()[1] * d[1] + _xyz()[2] * d[2]) - plate
    lean = (-_dot(n, d)).max(0).min(1).asin() * (180.0 / math.pi)
    margin = lean - limit - 1.0 - _BIG * (0.6 - height).max(0)
    if creases:
        margin = margin - _BIG * (1.0 - ((n.magnitude - 0.8) * (1.0 / 0.1)).max(0).min(1))
    return margin


# ---------------------------------------------------------------------------
# The shell: remedies.  Each is a field made from the part's own -- shifted copies of it -- and the remedies are all made from the part as it
# is (not from one another: shifted copies of shifted copies would multiply), then united: what a remedy adds, the union of what they add,
# and what they cut, taken off that.  Giving the result to the shell again is the next pass.

REMEDIES = ('undercut', 'draft', 'gaps', 'corners', 'walls', 'overhang')

_REMEDY_TEXT = OrderedDict([
    ('undercut', _tr('Undercut filled')),
    ('draft', _tr('Draft applied to walls')),
    ('gaps', _tr('Gap closed or widened')),
    ('corners', _tr('Inside corners filleted')),
    ('walls', _tr('Wall thickness increased')),
    ('overhang', _tr('Overhang supported (gusset or teardrop)')),
])
_REMEDY_RULE = {'undercut': 'undercut_along', 'draft': 'draft', 'gaps': 'min_gap', 'corners': 'min_radius', 'walls': 'min_wall', 'overhang': 'overhang'}


def _remedy_walls(ctx, t):
    ''' Where a wall is thinner than `t` the surface is offset outwards by what it lacks, half on each side, to `t` and no more: how thick the wall is at a point is the length
        of the line along the normal, from the nearest point of the surface through the material to where it meets the surface again, and the offset is a field of its own
        -- the part's field minus it --, so a wall grows only where it is thin, over the whole of its face, up to its edge and not past it.

        What it does NOT do, by decision and measurement: the faces of a thin wall are grown, the places where two faces meet are not filled.  Along an edge where thin walls meet, and in the
        inside corner where a thin wall stands on a thick face, a notch of the growth's size is left (0.15 mm for a wall of 0.5 asked 0.8).  Terms that filled them -- a strip of the end face
        of another wall taking the growth of the face beside it, a wedge in the inside corner -- were made and measured: they cost three to ten times the offset to draw, left faint ridges and
        slivers 1.2 mm from the edges of walls that were thick enough, and made the field one the viewer could not mesh within the memory of the machine (8 GB at 14 cells to the millimetre
        on an open box; the same box without them: 0.84 GB).  The corner rule (`min_radius`) rounds such corners '''
    f, n = ctx.field, ctx.normals
    chord = _chord(ctx, t, True)
    # (the line along the normal from the nearest point of the surface says something only where that point is on the surface -- over the mouth of a slot the normal leads to the air
    # between the two edges, and a gap would be taken for a wall; and where the slope of the field is not a distance's, on the middle of a gap, the normal is no face's)
    sure = ((n.magnitude - 0.8) * (1.0 / 0.1)).max(0).min(1) * _on_face(ctx, 0.02 * t)
    # (a wall the rule tolerates -- under the limit by the grace at most -- is not made thicker, but one that is under that is made the full thickness, and between the two the growth is
    # eased out, to nothing at the limit: the wall that comes out is then always more than the grace above what the check marks.  It used to be eased out BELOW the line the check marks at,
    # so that a wall of 0.76 to 0.78 came out as thick as it was, and the check, which reads a wall along lines a few degrees off the normal, marked it, with a few hundredths of a
    # millimetre of difference between the two ways of measuring: the two met at the same line)
    # (a line along the normal that leaves the material at once -- a chord under a tenth of the limit -- is the line of a point at an EDGE, grazing along the face beside it, and not a wall of
    # nothing: taken for one it grew the layer by half of the limit round the rim of a disc, a razor-thin flange 0.15 mm wide past its radius at the height of each face, and a lip above the
    # rim of an open box.  Counted for nothing below 0.064 of a 0.8 mm limit and in full from 0.096)
    valid = ((chord - 0.08 * t) * (1.0 / (0.04 * t))).max(0).min(1)
    gate = ((t - chord) * (1.0 / (GRACE * t))).max(0).min(1) * sure * valid
    main = ((t - chord) * 0.5).max(0).min(t / 2.0) * gate
    # (the surface moves by at most half the wall, and only where it is within that of the part's: the remedy is the part's own field beyond 0.6 of a wall thickness, which is what lets the
    # mesher skip every cell farther off without working through it)
    return f - _near(main, f, 0.6 * t, t / 2.0)


def _remedy_gaps(ctx, g, mode):
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
    step = grows(ctx.one_sided_normals('forward'), 1.0).max(grows(ctx.one_sided_normals('backward'), 2.0)).max(0)
    if mode == 'widen':
        return f + _near(step, f, 0.6 * g, g / 2.0)
    return f - _near(step, f, 0.6 * g, g / 2.0)


def _remedy_corners(ctx, radius):
    ''' The part with every inside corner rounded to `radius`: the library's `fillet` -- the part grown by a ball of that radius and taken back by it, each time by its true
        distance -- so the corner is filled by the arc of a ball that rolls in it: a circle of that radius whose slope turns smoothly from one face to the other (measured in
        the application on an L of two walls 3 thick, radius 6: the radius 6.000 at every degree of the arc from 2 to 88, and a degree of slope to a degree of angle).  A flat
        face, a bump and an outside corner are as they were, and the ends of a corner are not touched.  It is made from the part's surface (the part is meshed twice: 2.4 s for
        the L, and remembered by what the part is made of), and so it works on any part -- an import, a lattice, a pocket --, and a slot narrower than the ball is filled
        as the cutter leaves it.  The part's own field is kept where nothing is added '''
    return ctx.field.min(_fillet(ctx.field, radius, bounds=(ctx.lo, ctx.hi)))


def _remedy_undercut(ctx, a):
    ''' What lies both under part of the part and over part of it, along `a`, is filled: the field's smallest value at the places above each point, and at the places below it, and where both
        are inside the part the point is made part of it.  The places are at fixed heights along `a`, the same for every point (the first and the last just inside the two ends of the part),
        not counted from each point: counted from the point, a sliver of material thinner than the spacing -- the tip of a lip, a thin shelf -- is hit by the points whose count happens to land
        in it, and the fill's edge comes out as a staircase as high as the spacing times the slope of the face (0.4 mm on a 20-degree dovetail); at fixed heights the whole sliver is hit
        or missed together, and the edge of the fill is one straight wall.  A shelf thinner than the spacing (the part's length along `a` over 16) may still lie between two heights '''
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
    # (the surface moves by at most the part's height along the pull times the tangent of the draft: the remedy is the part's own field farther from the surface than that, and the mesher is told so
    # at once, as it is for the walls and the gaps.  Without the band the normals -- six lookups of the part -- were worked through in every cell of the picture)
    grow, width = _draft_reach(ctx, degrees, d)
    return f - _near(delta, f, width, grow)


def _draft_reach(ctx, degrees, d):
    ''' (the most the draft can move the surface, the width of the band round the surface that holds it): the part's height along the pull times the tangent of the draft, and a little more than
        that -- the ramp of `_near` must stay above what it limits where the growth is the greatest '''
    norm = math.sqrt(sum(float(x) ** 2 for x in d)) or 1.0
    grow = _extent_along(ctx, [float(x) / norm for x in d]) * math.tan(math.radians(degrees))
    return grow, 1.25 * grow + 2.0 * ctx.step


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
        # (everything under the plate is part of the part.  A little more than that: the part's own bottom face rests on the plate, so the field of the part and the height over the plate are both 0 on it, and a
        # disc point that lands exactly on that face reads 0 -- from both sides, the smaller of the two climbing to 0 and falling away.  The first disc, at the reach and the inset under a point of
        # solid with no air below it, lands there for the points one reach and an inset up (10.28 mm over the plate in o4): a sheet inside the material where the result touches 0 without crossing it,
        # drawn as a dotted line by the section and as degenerate triangles by the mesher.  Counted from four steps over the plate, the smaller of the two peaks at minus two steps)
        f = f.min(above - 4.0 * ctx.step)
        start = _entry(-f, d, -1.0, reach) + inset
        sign = -1.0
    # Discs at several depths, each counted from where the part starts above the point (not from fixed heights: those make the slope a stair of flat treads).  Each depth
    # is a field that changes smoothly with the point, and so does the best of them.  A disc further in is a wider one: it is the one that fits where the part is wide
    # a little further up (a round hole is wide below its top, and a pointed cap above it is made of discs that sit there)
    rings = []
    for fraction in _DISC_DEPTHS:
        s = start + reach * fraction
        points = [_at(f, s * sign * d[0], s * sign * d[1], s * sign * d[2])]            # (the centre)
        # (a wide disc is looked at half way out as well, between the points of its rim: eight points on a rim and one in the centre leave the middle of the disc unseen, and a disc 18 mm across, with
        # its rim all outside a small part and its centre in a channel, passed for "air all over" and cut a pocket of air out of the solid, 3 mm over the apex of the teardrop)
        for share, turn in ((1.0, 0.0),) if fraction < 0.25 else ((1.0, 0.0), (0.5, 0.5)):
            for j in range(k):
                ang = 2.0 * math.pi * (j + turn) / k
                ang_cos, ang_sin = math.cos(ang), math.sin(ang)
                dx = s * (sign * d[0] + share * tan * (ang_cos * e1[0] + ang_sin * e2[0]))
                dy = s * (sign * d[1] + share * tan * (ang_cos * e1[1] + ang_sin * e2[1]))
                dz = s * (sign * d[2] + share * tan * (ang_cos * e1[2] + ang_sin * e2[2]))
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


def _shell_points(ctx, width, count=40000, seed=17, give_up=4.0):
    ''' Points of the shell round the part's surface -- where its field is within `width` of 0 -- and the volume each stands for: (points, the part's field at them, volume),
        or None when there are none.  A remedy made with that width changes nothing outside the shell, so what it changes is measured on points that are all in it: random
        points over the whole box find a layer a tenth of a millimetre thick on a part of a hundred only a few times in a thousand, and a slot of 56 mm^3 in 175000 not at all.
        The points are drawn evenly over the box (_halton) and the ones in the shell kept: each stands for the box's volume over the number drawn.  Stops at `count` points, or
        after `give_up` seconds (a part whose field is slow to evaluate) '''
    rng = random.Random(seed)
    shift = (rng.random(), rng.random(), rng.random())
    pad = max(0.05 * ctx.size, width)            # (the shell reaches `width` past the part: a box that stops short of it would leave out the outer part of what a remedy adds)
    lo = [ctx.lo[i] - pad for i in range(3)]
    hi = [ctx.hi[i] + pad for i in range(3)]
    box = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])
    kept, values, drawn = [], [], 0
    started = time.time()
    batch = 30000
    while len(kept) < count and drawn < 6000000 and (drawn == 0 or time.time() - started < give_up):
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

    def _bodies(self):
        ''' What the shell changed, as bodies of their own: the part where it is as it was (grey), the material added (red: in the result and outside the input part) and the material
            cut (blue: in the input part and outside the result), each by its exact field -- the result with the input part, and nothing else.  A colour read at the vertices of the
            mesh of the result cannot show a layer that lies flush with the faces of the part (a slot that was closed, a pocket filled): the vertices of its flat face are all on the
            edges of the old surface, where nothing has changed, and no vertex is inside it to be red '''
        part, result = self.part, self._field
        adds, cuts = getattr(self, '_adds', True), getattr(self, '_cuts', True)
        tie = getattr(self, '_tie', 1e-4)
        hair = getattr(self, '_hair', 1e-3)       # (what is added lies at least this far outside the part: where the result is the part's own, `max(result, -part)` is 0 on the whole surface,
                                                  # and the mesher draws a skin of no thickness over every face that was not changed)
        if cuts and adds:
            kept = result.max(part - tie)         # (the result where it is the part's own, a few microns apart: equal values would be a tie, which the mesher is slow to settle)
        elif cuts:
            kept = result
        else:
            kept = part
        bodies = [(kept, 0.0)]
        if adds:
            bodies.append((result.max(hair - part), 1.0))
        if cuts:
            bodies.append((part.max(hair - result), -1.0))
        out = []
        for shape, value in bodies:
            box = getattr(result, '_bounds', None)
            if box and getattr(shape, '_bounds', None) is None:
                shape._bounds = box
            out.append((colored(shape, Shape.wrap(value), range=(-1.0, 1.0), label=_tr('Geometry change'), colormap='change'), value))
        return out

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
            note = _tr('Red and blue show what was added and removed compared with the input part.')
            rows = [(_tr('Material added'), _quantity(self.added, 3), 'mm³'), (_tr('Material removed'), _quantity(self.removed, 3), 'mm³'),
                    (_tr('Volume changed'), _quantity(100.0 * self.share, 3), _tr('% of the part')),
                    # (how much of the surface the places are, beside their number: four places of 0.08 % each read very differently from four places of a tenth of the part.  All the places the re-check
                    # found, and not only the five biggest of each rule that are listed: the card said 1.9 % of the surface where it found 6.9 %)
                    (_tr('Places still breaking a rule'), '%d' % self._places(),
                     ('(%s %s)' % (_quantity(100.0 * self._places_share(), 2), _tr('% of the surface'))) if self.fail_states else '')]
            if self.orientation is not None:
                # (the way up of the print was left free, and the tool found it: it is on the card, and `.oriented()` is the part turned to stand that way)
                rows.append((_tr('Way up of the print'), '(%.2g, %.2g, %.2g)' % tuple(self.build_direction), ''))
            if getattr(self, '_thin', False):
                # (a thin layer -- a wall thickened by a tenth of a millimetre, a gap made wider -- is thinner than a cell of the picture, and three bodies of which two are such skins
                # are drawn as stripes and missing patches.  The result is drawn as ONE body, whole and at the scene's resolution, coloured by the change at each point of its surface:
                # red where it lies outside the input part, blue where inside it, the part's colour on the old surface.)
                body = colored(self._field, self.change_map, range=(-1.0, 1.0), label=_tr('Geometry change'), colormap='change')
                body._color_fields = [('change', _tr('Geometry change'), self.change_map, -1.0, 1.0)]
                body._color_field_name = 'change'
                body._value_note = note
                body._key_results = rows
                box = getattr(self._field, '_bounds', None)
                if box and getattr(body, '_bounds', None) is None:
                    body._bounds = box
                shown = [body]
            else:
                bodies = self._bodies()
                shown = [body for body, _ in bodies]
                for body, value in zip(shown, [v for _, v in bodies]):            # (each body is the same result, with the one card: a body of its own colour with no card would have a bar of its own)
                    body._color_fields = [('change', _tr('Geometry change'), Shape.wrap(value), -1.0, 1.0)]
                    body._color_field_name = 'change'
                    body._value_note = note
                    body._key_results = rows
            if getattr(self, 'supports', None) is not None:
                # (ONE class, amber, and a card with one swatch that says what it is: a scale from -1 to 1 for a body of one colour said nothing)
                shown.append(colored(self.supports, Shape.wrap(1.0), range=(-1.0, 1.0), label=_tr('Supports'), colormap='support'))
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

        Returns a ManufacturingResult: a body that is the part changed, coloured by the geometry change, with `.changes`, `.added`, `.removed`, `.fail_states`.
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
    _progress(0.02, _tr('Reading part'))
    _log(t0, 'the part is read')
    _progress(0.05, _tr('Measuring surface'))
    before = _analyse(shape, items, int(samples), what='manufacturing_optimization')           # (the check of the part as it is: its way up, its points, what breaks)
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
    _progress(0.2, _tr('Measuring part volume'))
    _log(t0, 'the volume of the part is measured')

    candidates = []                  # (remedy, rule, field, adds, the width of the shell it changes -- or None when it can reach anywhere)
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
                candidates.append(('walls', rule, scoped(_remedy_walls(ctx, value)), True, value))
            elif rule == 'min_gap' and 'gaps' in allowed:
                candidates.append(('gaps', rule, scoped(_remedy_gaps(ctx, value, c.gap_fix)), c.gap_fix == 'fill', value))
            elif rule == 'min_radius' and 'corners' in allowed:
                # (the library's fillet is the distance to a mesh, good to about a hundredth of a millimetre: over a smooth part that has no corner to round it is the part's own field to that much and adds
                # specks that thick wherever the mesh falls short -- a torus came out with 3.9 mm^3 "added" and a few red dots, and a part with no inside corner paid for the two meshes of the fillet.  So the remedy
                # is made where the check of the part finds a corner short of the radius at one of its samples, and not where it finds none; the other remedies are exact fields, which are the part's own
                # where nothing is wrong, and are made as ever)
                if before.report.get(rule, {}).get('broken', 1) == 0:
                    candidates.append(('corners', rule, None, True, value))
                else:
                    candidates.append(('corners', rule, scoped(_remedy_corners(ctx, value)), True, value))        # (a ball of the radius finds nothing to fill where the air is more than a radius from the part)
            elif rule == 'undercut_along' and 'undercut' in allowed:
                candidates.append(('undercut', rule, scoped(_remedy_undercut(ctx, value)), True, None))
            elif rule == 'draft' and 'draft' in allowed:
                # (the growth is at most the part's height along the pull times the tangent of the draft, and lies in a band that wide round the surface: measured on points of that shell, like the layers of
                # the walls and the gaps.  Measured on twelve thousand points over the whole box it was read to 5 %, and the card said 1,321 mm^3 of a change of 1,180 on the moulding sampler)
                candidates.append(('draft', rule, scoped(_remedy_draft(ctx, value, c.pull_direction)), True, _draft_reach(ctx, value, c.pull_direction)[1]))
            elif rule == 'overhang' and 'overhang' in allowed:
                candidates.append(('overhang', rule, scoped(_remedy_overhang(ctx, value, d, c.overhang_fix, reach)), c.overhang_fix == 'fill', None))
    changes = []
    adds, cuts = [], []
    measured = []                    # (entry, field, the points it was measured on, the part's values there, the volume of a point, a shell?) of each kept additive remedy
    spent = 0.0
    shells = {}
    for index, (remedy, rule, field, additive, width) in enumerate(candidates):
        if field is None:                    # (a remedy that was not made: the check finds nothing for it to do)
            changes.append({'remedy': remedy, 'rule': rule, 'added': 0.0, 'removed': 0.0, 'kept': False, 'why': _tr('no change required'), 'thin': False})
            continue
        _progress(0.25 + 0.5 * index / max(1, len(candidates)), _tr('Measuring effect: %s') % _REMEDY_TEXT[remedy])
        # (what a remedy of a known width changes lies in the shell of that width round the surface: measured on points of the shell, so that a thin layer or a narrow gap has
        # points in it; one that can reach anywhere on points over the whole box)
        shell = None
        if width is not None:
            if width not in shells:
                shells[width] = _shell_points(ctx, width)
            shell = shells[width]
        if shell is not None:
            where, before_values, each = shell
        else:
            where, before_values, each = pts, f0_vals, box / float(len(pts))
        vals = _values(field, where)
        added = each * sum(1 for a, b in zip(before_values, vals) if a > 0 >= b)
        removed = each * sum(1 for a, b in zip(before_values, vals) if a <= 0 < b)
        if shell is None:
            refined = _refined_change(ctx, field, where, before_values, vals, each)
            if refined is not None:
                added, removed = refined
        entry = {'remedy': remedy, 'rule': rule, 'added': added, 'removed': removed, 'kept': False, 'why': '',
                 'thin': remedy == 'walls' or (remedy == 'gaps' and not additive)}       # (a layer a tenth of a millimetre thick: see ManufacturingResult._display)
        _log(t0, 'remedy %s measured: +%.4g -%.4g mm^3' % (remedy, added, removed))
        if added + removed < 1e-4 * volume:
            entry['why'] = _tr('no change required')
        elif spent + added + removed > budget:
            entry['why'] = _tr('would change %.2g %% of the part volume; max_change allows %.2g %%: add a support or change the design') % (
                100.0 * (spent + added + removed) / volume, 100.0 * max_change)
        else:
            entry['kept'] = True
            spent += added + removed
            (adds if additive else cuts).append(field)
            if additive:
                measured.append((entry, field, where, before_values, each, shell is not None))
        changes.append(entry)
    # (what the result adds is the volume of the UNION of what the kept additive remedies add, not the sum of what each adds alone: where two overlap -- a draft wedge and an undercut fill on one wall, a
    # fillet at the root of a thickened wall -- the sum counts the place twice, and the card said 1516 mm3 of a change of 1180.  Each remedy after the first is measured for what it adds that the ones before it
    # did not, on the points it was measured on)
    union_added = None
    if len(measured) > 1:
        union_added, earlier = 0.0, None
        for entry, field, where, before_values, each, is_shell in measured:
            if earlier is None:
                marginal = entry['added']
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
    result = ctx.field
    # (a remedy is the part's own field where it does nothing: the same value as the part, over most of space -- a tie -- so each is kept a few microns apart; see _join)
    for i, a in enumerate(adds):
        result = result.min(a + ctx.tie * (i + 1))
    for i, c in enumerate(cuts):
        # (a cut remedy is the whole part with something cut away, and what it cuts is the part where it is positive: the difference takes away that region only.  Intersecting the
        # result with the cut remedy as a whole would cut away everything that the additive remedies built outside the part)
        # (the part's field less a skin of `step`: the cut then reaches a hair outside the part's old surface, where the remedy says "air" as well.  Cut exactly to that surface, the
        # cut and the part share it, and the difference between them is exactly 0 on it -- touching 0 without crossing it, a ghost surface inside the new air that the mesher draws as
        # triangles of no area and the export as edges of nobody: seen on a slot made wider, whose old faces read exactly 0.0 inside the new gap)
        removed = (ctx.field - ctx.step).max(-c)             # (negative where the part is cut away, and nowhere else)
        result = result.max(-removed - ctx.tie * (i + 1))
    # (the whole result grown by a hair, 5 ties -- under a tenth of a micron: where the part's faces lie exactly on the grid the picture is drawn on (integer coordinates, as scripts have
    # them), a surface that is the part's own gives exactly 0 at the grid points on it, and where a remedy's surface meets it at an edge there are several surfaces through the
    # same grid point, which the mesher settled with edges of more than two triangles and triangles of no area.  Grown by a hair no grid point is exactly on a surface)
    result = result - 5.0 * ctx.tie
    result._bounds = (tuple(ctx.lo[i] - 0.05 * ctx.size for i in range(3)), tuple(ctx.hi[i] + 0.05 * ctx.size for i in range(3)))
    # (the plate the print stands on is the part's box, not the result's padded one: a check or a second pass made on the result reads it from here, and the bottom face of the part, resting on
    # the plate, is no overhang -- read from the padded box it stood 5 % of the part's size above the plate and was a flat roof: 6 % to 15 % of the surface of the result "overhanging")
    result._plate_box = ctx.plate_box
    for name in ('_locks', '_free'):
        v = getattr(shape, name, None)
        if v is not None:
            setattr(result, name, v)
    tol = max(0.05, 2.0 * ctx.step)
    # (the part's own field, at the point carried onto the result's surface -- the point less the result's value there along the result's normal: a vertex of the drawn mesh is a fraction
    # of a cell off the true surface, and on a layer a tenth of a millimetre thick that is the other side of the old surface -- blue where nothing was cut, seen on the walls of the holes
    # of a perforated plate.  On the surface itself the carry is nothing.  The carry runs along the PART's normal, not the result's: the remedies offset along it, and the result's own normals
    # are six more copies of the whole result inside the field the part is coloured by -- the viewer builds evaluators for that field when the mouse rests on the result, memory in proportion
    # to its size: 8 GB and a window that stopped answering on an open box)
    carry = ctx.normals
    change_map = (_at(ctx.field, -result * carry.x, -result * carry.y, -result * carry.z) * (1.0 / tol)).max(-1).min(1)
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
        after = _analyse(result, items, int(samples), bounds=result._bounds, plate_box=ctx.plate_box, direction=direction, seed=13, rounds=1, batch_size=20000,
                        seeds=before.points, supports=supports)
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
    if union_added is not None:
        out.added = union_added
        out.share = (out.added + out.removed) / volume if volume > 0 else 0.0
    out.fail_totals = fail_totals
    out.supports = supports
    out.support_volume = support_volume
    out.blockers = blockers
    out.change_tolerance = tol          # (mm: the distance from the input surface at which the change map reaches +1 or -1)
    out._adds, out._cuts, out._tie, out._hair = bool(adds), bool(cuts), ctx.tie, 1e-4 * ctx.size
    out._thin = any(c['kept'] and c.get('thin') for c in changes)
    return out


manufacturing_optimization._required = ('constraints',)
