'''
Manufacturing, the check: where a part breaks the rules of its constraints (manufacturing_analysis).

THE CHECK IS INDEPENDENT OF THE OPTIMIZATION (manufacturing_optimization, in manufacturing.py), so that it can verify it.  This file imports the rules
(manufacturing_rules.py) and the tools that read a part's field (manufacturing_field.py), and nothing of the optimization: the remedies do not appear here, and what
makes the remedies find a thin wall or a leaning face is not what finds it here.  The optimization calls `_analyse` (below) to be told what its result still breaks, and
that is the only way it reaches into this file.  Where a rule is measured here, it is measured by a way of its own:

    wall thickness, gap width   the thinnest straight line through the material (the air, for a gap) from the surface point, in the cone of 20 degrees round the normal
                                (the optimization measures along the normal alone)
    overhang, draft             the normals of the surface against the way up, and the pull direction
    inside corner radius        whether a ball of the radius reaches into the corner (the optimization closes the part with the library's fillet)
    undercut, bridge            lines along the direction from the surface point, against the part

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
from fieldes.stdlib.manufacturing_field import (_BIG, _Context, _at, _dot, _extent_along, _frame, _join, _remembered, _sphere, _values, _where, _xyz)
from fieldes.stdlib.manufacturing_rules import GRACE, ManufacturingError, _RULE_TEXT, _STRICTEST, _constraints_given, _progress

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


def _tangents(n):
    ''' Two unit vectors along the surface at every point, as fields: with the normal a frame, from a direction that is not often along a normal '''
    ref = (0.30, 0.50, 0.81)
    cx = n.y * ref[2] - n.z * ref[1]
    cy = n.z * ref[0] - n.x * ref[2]
    cz = n.x * ref[1] - n.y * ref[0]
    cm = (cx * cx + cy * cy + cz * cz).sqrt().max(1e-3)
    t1 = (cx / cm, cy / cm, cz / cm)
    t2 = (n.y * t1[2] - n.z * t1[1], n.z * t1[0] - n.x * t1[2], n.x * t1[1] - n.y * t1[0])
    return t1, t2


def _flatness(ctx, reach_):
    ''' 1 where the surface is flat within `reach_` of a point and 0 where it turns -- an edge, a corner, a tight curve: the normal at four points round it (`reach_`
        off along the surface) against its own, the least of the four, from 0.8 (0) to 0.9 (1) of a dot product.  The thickness along a normal means a wall's
        thickness where the surface is flat, and not on the rounded edge of a thin wall, where the normal looks across the corner '''
    n = ctx.normals
    x, y, z = _xyz()
    t1, t2 = _tangents(n)
    least = None
    for t in (t1, t2):
        for sign in (1.0, -1.0):
            dx, dy, dz = t[0] * (sign * reach_), t[1] * (sign * reach_), t[2] * (sign * reach_)
            nx, ny, nz = n.x(x + dx, y + dy, z + dz), n.y(x + dx, y + dy, z + dz), n.z(x + dx, y + dy, z + dz)
            dot = n.x * nx + n.y * ny + n.z * nz
            least = dot if least is None else least.min(dot)
    return ((least - 0.8) * (1.0 / 0.1)).max(0).min(1)


def _rule_margins(ctx, constraints, direction):
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


def _surface_samples(ctx, count=5000, seed=7, rounds=10, batch_size=150000, seeds=None):
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
        kept = [item for item, vo, vi in zip(candidates, vals[:k], vals[k:]) if vo > 0 >= vi]
        found.extend(_even_over_area(f, kept, band, [lo[i] - pad for i in range(3)], [hi[i] + pad for i in range(3)], rng))
        if len(found) >= count:
            break
    return found[:count]


def _crossing(vals, ds, into_part):
    ''' How far along a line of probes (values `vals` at the distances `ds`, from a point of the surface) the field changes sign: into the part (the values fall to 0 or below),
        or out of it (they rise above 0), by the straight line between the two probes it falls between.  None when it does not within the line.  The first probe already
        across gives its own distance '''
    previous_s, previous_v = 0.0, None
    for s, v in zip(ds, vals):
        across = (v <= 0) if into_part else (v > 0)
        if across:
            if previous_v is None:
                return s
            a, b = previous_v, v
            return previous_s + (s - previous_s) * (a / (a - b) if into_part else (-a) / (b - a))
        previous_s, previous_v = s, v
    return None


def _even_over_area(f, items, band, box_lo, box_hi, rng):
    ''' The samples of one round, thinned so that they are even over the SURFACE.  The points were drawn at random in a band round the surface and moved onto it, so a
        face has as many samples as the band round it holds: a face in a thin wall has a band that stops at the other face, a face on a narrow gap likewise, a face at the
        edge of the region meets the edge of it -- and has fewer samples per mm^2 than a face in the open, and a share counted on the samples (how much of the surface
        breaks a rule) is too small for the thin and the narrow, which are what the rules are about.  The length of band each sample stands for -- along the normal into the
        part (to the nearest other face, or the band's width) and into the air, and not past the region -- is found by eight probes each way, and a sample is kept with the
        chance that makes them all stand for the same.  items: [(point, normal, gradient size, value)] '''
    if not items:
        return items
    steps = 8
    ds = [band * (k + 1) / float(steps) for k in range(steps)]
    probes = []
    for p, n, _m, _v in items:
        for s in ds:
            probes.append((p[0] + s * n[0], p[1] + s * n[1], p[2] + s * n[2]))
        for s in ds:
            probes.append((p[0] - s * n[0], p[1] - s * n[1], p[2] - s * n[2]))
    vals = _values(f, probes)
    weights = []
    for i, (p, n, _m, _v) in enumerate(items):
        out_vals = vals[2 * steps * i: 2 * steps * i + steps]
        in_vals = vals[2 * steps * i + steps: 2 * steps * (i + 1)]
        gap = _crossing(out_vals, ds, True)          # (the air is a gap: the part is across it)
        thick = _crossing(in_vals, ds, False)        # (the part is a wall: the air is across it)
        b_out = band if gap is None else min(band, 0.5 * gap)
        b_in = band if thick is None else min(band, 0.5 * thick)
        for sign, which in ((1.0, 'out'), (-1.0, 'in')):
            reach = 1.0e9
            for a in range(3):
                d = sign * n[a]
                if d > 1e-9:
                    reach = min(reach, (box_hi[a] - p[a]) / d)
                elif d < -1e-9:
                    reach = min(reach, (box_lo[a] - p[a]) / d)
            if which == 'out':
                b_out = min(b_out, max(reach, 0.0))
            else:
                b_in = min(b_in, max(reach, 0.0))
        weights.append(max(b_out + b_in, 1e-6))
    ordered = sorted(weights)
    w_ref = ordered[int(0.05 * (len(ordered) - 1))]
    return [item for item, w in zip(items, weights) if rng.random() * w <= w_ref]


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


# ---------------------------------------------------------------------------
# The numbers of a report are worked out on the sample points themselves, by a second implementation of each rule -- plain arithmetic on
# the values of the part's field at points along the lines each rule looks along -- which does not build the (deeply nested) fields at all, and is
# a check of those fields: the colours on the part and the numbers in the report are made two ways (for a wall and a gap, by two methods: see _num_thickness)

def _sub(pts, base, direction, s):
    return [(p[0] + s * direction[i][0], p[1] + s * direction[i][1], p[2] + s * direction[i][2]) for p, i in zip(pts, base)]


_CONE = math.radians(20.0)
_FACING = 0.8           # (the cosine of the most a face may lean away from facing back at a point: about 37 degrees)


def _num_thickness(f, pts, normals, offsets, limit, inward, h):
    ''' How thick the wall is (how wide the gap is) at each point, for the report: the shortest line through the material (the air, for a gap) from the point, among nine --
        the normal, and eight leaning 20 degrees away from it all round -- each taken as how far apart the two faces are along the normal (the length of the line times the
        cosine of its lean: on a flat wall every one of them gives its thickness).  A line is followed through 14 places, and ends where the field crosses 0; up to a tenth
        more than `limit` is looked for.  A thin place the normal alone passes over -- the wall narrows to one side, a face across it leans -- is found by the lines at
        its side.  The face a line ends on has to face back at the point (its normal within about 37 degrees of the opposite of the point's own): that is what a wall or a
        gap is, two faces facing each other.  A face that meets this one at a corner -- the foot of a wall on a plate, the edge of a block -- is not across from it, and a line
        leaning towards it is no thin place.  All the places of all the points are one evaluation of the field (and the faces the lines end on a second, `h` the step of the
        differences); `offsets` are the field's values at the points (they are moved onto the surface first).
        This is measured with a method of its own, and not the one the remedies find a thin wall by (the line along the normal): what one of them misses the other may find '''
    count = 14
    rays = 9
    cosine = math.cos(_CONE)
    sign = -1.0 if inward else 1.0
    flip = 1.0 if inward else -1.0
    reach_ = 1.1 * limit / cosine
    m = len(pts)
    onto = [(p[0] - v * n[0], p[1] - v * n[1], p[2] - v * n[2]) for p, v, n in zip(pts, offsets, normals)]
    directions = []                                    # (for each point: the nine unit vectors, the first along the normal)
    for n in normals:
        e1, e2 = _frame(n)
        around = [(cosine * sign * n[a] + math.sin(_CONE) * (math.cos(2.0 * math.pi * j / 8.0) * e1[a] + math.sin(2.0 * math.pi * j / 8.0) * e2[a])) for j in range(8) for a in range(3)]
        directions.append([(sign * n[0], sign * n[1], sign * n[2])] + [tuple(around[3 * j: 3 * j + 3]) for j in range(8)])
    steps = [reach_ * k / float(count) for k in range(1, count + 1)]
    flat = []
    for r in range(rays):
        for s in steps:
            flat.extend((q[0] + s * d[r][0], q[1] + s * d[r][1], q[2] + s * d[r][2]) for q, d in zip(onto, directions))
    vals = _values(f, flat)
    eps = 1e-3 * limit
    found = []                                         # (the point, how far apart the faces are along the normal, where the line ends)
    for i in range(m):
        for r in range(rays):
            previous, s_prev = -eps, 0.0
            for k, s in enumerate(steps):
                b = flip * vals[(r * count + k) * m + i]
                if previous < 0 <= b:
                    length = s_prev + (s - s_prev) * (-previous) / (b - previous + 1e-9)
                    d = directions[i][r]
                    found.append((i, length * (1.0 if r == 0 else cosine), (onto[i][0] + length * d[0], onto[i][1] + length * d[1], onto[i][2] + length * d[2])))
                    break
                previous, s_prev = b, s
    out = [1.1 * limit] * m
    if found:
        for (i, along, _), (_, g) in zip(found, _gradients(f, [q for _, _, q in found], h)):
            size = math.sqrt(g[0] ** 2 + g[1] ** 2 + g[2] ** 2)
            n = normals[i]
            if size > 1e-9 and -(g[0] * n[0] + g[1] * n[1] + g[2] * n[2]) / size >= _FACING and along < out[i]:
                out[i] = along
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


def _numeric_margins(ctx, constraints, direction, samples, supports=None):
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
            if rule == 'overhang':
                plate = min(x * d[0] + y * d[1] + z * d[2] for x in (ctx.plate_box[0][0], ctx.plate_box[1][0]) for y in (ctx.plate_box[0][1], ctx.plate_box[1][1])
                            for z in (ctx.plate_box[0][2], ctx.plate_box[1][2]))
                m = []
                flat = _num_flat(f, pts, normals, 3.0 * ctx.step, ctx.step)
                for (p, nn), fl in zip(zip(pts, normals), flat):
                    phi = math.degrees(math.asin(max(0.0, min(1.0, -(nn[0] * d[0] + nn[1] * d[1] + nn[2] * d[2])))))
                    height = p[0] * d[0] + p[1] * d[1] + p[2] * d[2] - plate
                    m.append(phi - value - 1.0 - (_BIG * (0.6 - height) if height < 0.6 else 0.0) - (0.0 if fl else _BIG))
                if supports is not None and c.support:
                    # (a place that leans out and has a support under it is held: a point just inside the top of the support, straight under it -- the support is `support_gap` from the part)
                    below = [(p[0] - d[0] * (c.support_gap + 2.0 * ctx.step), p[1] - d[1] * (c.support_gap + 2.0 * ctx.step), p[2] - d[2] * (c.support_gap + 2.0 * ctx.step)) for p in pts]
                    held = [v <= 0 for v in _values(supports, below)]
                    m = [(-_BIG if h else x) for x, h in zip(m, held)]
            elif rule == 'min_wall':
                flat = _num_flat(f, pts, normals, 0.5 * value, ctx.step)
                m = [(value - ch - GRACE * value) if fl else -_BIG for ch, fl in zip(_num_thickness(f, pts, normals, offsets, value, True, ctx.step), flat)]
            elif rule == 'min_gap':
                flat = _num_flat(f, pts, normals, 0.5 * value, ctx.step)
                m = [(value - ch - GRACE * value) if fl else -_BIG for ch, fl in zip(_num_thickness(f, pts, normals, offsets, value, False, ctx.step), flat)]
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


def _report_numeric(ctx, constraints, direction, samples, supports=None):
    ''' The report of `samples` for the rules of the constraints: {rule: {limit, unit, broken, share, worst, regions, samples}} '''
    margins, limits = _numeric_margins(ctx, constraints, direction, samples, supports)
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

        Stated on its own the part is shown red where a rule is broken and in its own colour where they are kept (the card switches to one rule).  It is a check of
        the part's own field, not a substitute for the check of the one who makes it.  Nothing of the optimization is used to make it. '''

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
        self.points = []                   # (the points of the surface that were measured, when it was made by `_analyse`)
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
            # (a rule check is red where a rule is broken and the part's own colour where it is kept -- two classes, no scale; what a rule measures is in its own unit, read by hovering)
            broken = (self._fields['all'] * 200.0).max(0).min(1)
            label = _tr('All rules (1 = broken)')
            out = colored(self.part, broken, range=(0.0, 1.0), label=label, colormap='violation')
            out._color_fields = [('all', label, broken, 0.0, 1.0)]
            out._value_note = _tr('Red = a rule is broken here. Hover to see by how much.')
            for rule, field in self._fields.items():
                if rule == 'all':
                    continue
                limit = self.limits[rule]
                scale = limit[0] if (limit[1] and rule != 'undercut_along') else 1.0
                out._color_fields.append((rule, _EXCESS_TEXT[rule], field.max(0), 0.0, 0.005 * abs(scale)))
            out._color_field_name = 'all'
            rows = []
            for rule, r in self.report.items():
                rows.append((_RULE_TEXT[rule][0], '%.3g %%' % (100.0 * r['share']), _tr('of surface area')))
            rows.insert(0, (_tr('All rules kept'), _tr('yes') if self.compliant else _tr('no'), ''))
            out._key_results = rows            # (a row for every rule asked: seven at most)
            self._shown = out
        return self._shown

    def __repr__(self):
        lines = [_tr('Manufacturing analysis: %d surface samples (%.1f s)') % (self.samples, self.seconds)]
        lines.append(_tr('  All rules satisfied') if self.compliant else _tr('  Rule violations:'))
        for rule, r in self.report.items():
            word, unit = _RULE_TEXT[rule]
            limit = ('(%.3g, %.3g, %.3g)' % tuple(r['limit'])) if rule == 'undercut_along' else ('%.4g %s' % (r['limit'], unit))
            if r['broken'] == 0:
                lines.append(_tr('    %s (limit %s): satisfied') % (word, limit))
                continue
            lines.append(_tr('    %s (limit %s): %.2g %% of surface area, max. deviation %.3g %s, %d region(s)') % (word, limit, 100.0 * r['share'], r['worst'], unit, len(r['regions'])))
            for g in r['regions'][:6]:
                lines.append('        %s  %.2g %%' % (_where(g['at']), 100.0 * g['share']))
        lines.append(_tr('  Evaluated on the part\'s own field; not a substitute for the manufacturer\'s own check.'))
        return '\n'.join(lines)


def _all_field(margins):
    ''' The worst rule at each point, as a share of its limit, 0 where all are kept (what the part is coloured by) '''
    out = None
    for rule, (field, limit, unit) in margins.items():
        scale = 1.0 if rule == 'undercut_along' else limit
        share = (field * (1.0 / scale)).max(0).min(1.5)
        out = share if out is None else out.max(share)
    return out


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


def _analyse(part, constraints, samples=5000, bounds=None, plate_box=None, direction=None, seed=7, rounds=10, batch_size=150000, seeds=None, supports=None, what='manufacturing_analysis',
            progress=False):
    ''' The check of `part` for the rules of `constraints`: a ManufacturingAnalysis (its `points` are the points of the surface it measured), or None where the part has no surface to
        measure.  The one way the optimization reaches into this file, to be told what its result still breaks: the check of a result is made here, with the measurements of the
        check, and not by what made the result.  `supports` (a field, negative inside) are the supports under what leans out: a place with a support under it is held.
        `bounds`, `plate_box`, `direction`, `seed`, `rounds`, `batch_size` and `seeds` are how the optimization reads the result it made: in the box of the part, standing on the
        plate of the part it was made from, the way up it was checked in, from the points of the part as it was '''
    t0 = time.time()
    items = _constraints_given(constraints, what)
    shape = Shape.wrap(part)
    ctx = _Context(shape, bounds)
    if plate_box is not None:
        ctx.plate_box = plate_box
    if progress:
        _progress(0.05, _tr('Measuring surface'))
    points = _surface_samples(ctx, int(samples), seed=seed, rounds=rounds, batch_size=batch_size, seeds=seeds)
    if not points:
        return None
    if progress:
        _progress(0.4, _tr('Evaluating rules'))
    if direction is None:
        direction = _direction_of(items, ctx, points)
    margins = _rule_margins(ctx, items, direction)
    if not margins:
        raise ManufacturingError('%s: the constraints have no rule -- give one (min_wall=0.8, overhang=45 ...)' % what)
    fields = OrderedDict((rule, m[0]) for rule, m in margins.items())
    fields['all'] = _all_field(margins)
    report = _report_numeric(ctx, items, direction, points, supports)
    out = ManufacturingAnalysis(part if isinstance(part, Shape) else shape, items, fields, {r: (m[1], m[2]) for r, m in margins.items()}, report, len(points), direction, time.time() - t0)
    out.points = [p[0] for p in points]
    return out


@_with_conditions
@_remembered('manufacturing_analysis')
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
    out = _analyse(part, constraints, samples, progress=True)
    if out is None:
        raise ManufacturingError('manufacturing_analysis: the part has no surface to measure (is it empty?)')
    return out


manufacturing_analysis._required = ('constraints',)


# What a rule's view shows on the card: how far the surface is past the limit, in the unit of the rule (0 where it is kept)
_EXCESS_TEXT = {
    'overhang': _tr('Overhang too steep by (degrees)'),
    'min_wall': _tr('Wall too thin by (mm)'),
    'min_gap': _tr('Gap too narrow by (mm)'),
    'max_bridge': _tr('Bridge too long by (mm)'),
    'min_radius': _tr('Inside corner too sharp: radius short by (mm)'),
    'draft': _tr('Draft too small by (degrees)'),
    'undercut_along': _tr('Undercut (above 0 = undercut)'),
}
