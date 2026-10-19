'''
Manufacturing, reading a part's field: lookups of the field at moved points, its normals from the field's symbolic derivative, its values at many points, the box the part is in.

These are the tools both the check (manufacturing_check.py) and the remedies (manufacturing.py) read a part with; there is no rule in them, and nothing here says
what is broken or how to fix it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import functools
import math
import time
import os
import random

from fieldes.shape import Shape
from fieldes.stdlib.content_cache import cache_for, value_key, Uncacheable
from fieldes.stdlib.distance_field import partials as _partials
from fieldes.stdlib.excluded import locks_of
from fieldes.stdlib.fields import _shape_bounds

# ---------------------------------------------------------------------------
# Fields made of shifted copies of the part's own field

def _xyz():
    return Shape.X(), Shape.Y(), Shape.Z()


def _at(f, dx, dy, dz):
    ''' f(p + (dx, dy, dz)): the field asked at the point moved by the vector (numbers or fields).  A call: the part's field is kept where it is
        and asked at the place, not copied for it -- the rules ask it at hundreds of places, which as copies were hundreds of copies of the part '''
    x, y, z = _xyz()
    return f(x + dx, y + dy, z + dz)


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
    ''' The gradient of a field and its unit vector, as fields: what faces which way, at every point '''

    def __init__(self, f, step, kind='central', shift=None):
        ''' The gradient is read from the field's symbolic derivative (`partials`: the kernel derives the field's expression, and its intervals over a cell are the derivative's own); for a
            field with a native field in it, by central differences of calls over `step`.  `kind`: 'central' (the default), or 'forward' / 'backward'.
            Central: each partial is the mean of the derivative asked at the two ends of the segment p +- h along its own axis -- what a central difference is, the mean slope over the segment --
            and not asked at p itself, where on a surface or an edge the derivative is the mean of the rules' on both sides (half a face's).  On the middle of a slot between two faces the field
            has a kink (the distance to the nearer face): a segment across it reads a slope from 0 (exactly on the middle: no normal) to 1 over a band of one step on either side, and the one-sided
            readings -- the derivative asked half a step ahead, or behind, along each axis, the mean slope over (p, p + h) and (p - h, p) --  each take the slope on one side of the middle:
            within a step of it one of them leads to the wrong face, and of the two one always leads to a wall.  Where the surface is smooth they differ from the central one by what the
            curvature times the step makes: a few thousandths of a degree.
            `shift` (a vector of numbers or fields) reads the normals of the point moved by it, at that point: the normal of a point that is looked at from another one '''
        self.step = step
        h = step
        sx, sy, sz = (0.0, 0.0, 0.0) if shift is None else shift
        partials = _partials(f)
        if partials is not None:
            fx, fy, fz = partials
            if kind == 'forward' or kind == 'backward':
                ahead = 0.5 * h if kind == 'forward' else -0.5 * h
                gx = _at(fx, sx + ahead, sy, sz)
                gy = _at(fy, sx, sy + ahead, sz)
                gz = _at(fz, sx, sy, sz + ahead)
            else:
                gx = (_at(fx, sx + h, sy, sz) + _at(fx, sx - h, sy, sz)) * 0.5
                gy = (_at(fy, sx, sy + h, sz) + _at(fy, sx, sy - h, sz)) * 0.5
                gz = (_at(fz, sx, sy, sz + h) + _at(fz, sx, sy, sz - h)) * 0.5
        else:
            here = f if shift is None else _at(f, sx, sy, sz)
            if kind == 'forward':
                gx = (_at(f, sx + h, sy, sz) - here) * (1.0 / h)
                gy = (_at(f, sx, sy + h, sz) - here) * (1.0 / h)
                gz = (_at(f, sx, sy, sz + h) - here) * (1.0 / h)
            elif kind == 'backward':
                gx = (here - _at(f, sx - h, sy, sz)) * (1.0 / h)
                gy = (here - _at(f, sx, sy - h, sz)) * (1.0 / h)
                gz = (here - _at(f, sx, sy, sz - h)) * (1.0 / h)
            else:
                gx = (_at(f, sx + h, sy, sz) - _at(f, sx - h, sy, sz)) * (0.5 / h)
                gy = (_at(f, sx, sy + h, sz) - _at(f, sx, sy - h, sz)) * (0.5 / h)
                gz = (_at(f, sx, sy, sz + h) - _at(f, sx, sy, sz - h)) * (0.5 / h)
        self.magnitude = (gx * gx + gy * gy + gz * gz).sqrt()
        # (on the middle of a gap or a wall the slope is 0 and there is no normal: a fixed direction stands in for it there, and nowhere the slope is a few thousandths or more)
        bias = 1.0 - (self.magnitude * 1000.0).min(1)
        gx, gy, gz = gx + bias * 0.57735, gy + bias * 0.57735, gz + bias * 0.57735
        m = (gx * gx + gy * gy + gz * gz).sqrt().max(1e-6)
        self.x, self.y, self.z = gx / m, gy / m, gz / m
        self.scale = self.magnitude.max(0.25).min(4.0)        # (what a field's value is divided by to be a distance, as far as it is told by its slope)


def _dot(n, d):
    return n.x * d[0] + n.y * d[1] + n.z * d[2]


def _tangents(n):
    ''' Two unit vectors along the surface at every point, as fields: with the normal a frame, made from a direction that is seldom along a normal '''
    ref = (0.30, 0.50, 0.81)
    cx = n.y * ref[2] - n.z * ref[1]
    cy = n.z * ref[0] - n.x * ref[2]
    cz = n.x * ref[1] - n.y * ref[0]
    cm = (cx * cx + cy * cy + cz * cz).sqrt().max(1e-3)
    t1 = (cx / cm, cy / cm, cz / cm)
    t2 = (n.y * t1[2] - n.z * t1[1], n.z * t1[0] - n.x * t1[2], n.x * t1[1] - n.y * t1[0])
    return t1, t2


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


_BIG = 1.0e3
_ODD_STEP = 1.01731         # (the factor that makes the normals' step no round number: see _Context)


class _Context:
    ''' Everything the fields of the rules are made from for one part: its field, how big it is, its normals '''

    def __init__(self, part, bounds=None):
        self.field = Shape.wrap(part)
        carried = getattr(self.field, '_plate_box', None)       # (a result of the shell: the plate of the part it was made from; read before the field is rescaled below)
        lo, hi = _shape_bounds(self.field, bounds)
        self.lo, self.hi = tuple(float(c) for c in lo), tuple(float(c) for c in hi)
        self.size = max(self.hi[i] - self.lo[i] for i in range(3)) or 1.0
        # (the rules and the remedies are read in millimetres off the field's own value -- how far a point is from the surface, how far to move along its normal --, which is true of a field
        # that is a distance and not of one that is a distance times something: a part that was scaled, a body made by a formula.  The typical slope of the field at its surface is read, and a
        # field whose slope is not 1 is divided by it, once, here: the same surface, a field that is a distance to a few per cent)
        self.slope = _typical_slope(self.field, self.lo, self.hi, self.size)
        # (a part held as `exact_distance` carries the plain field it was made from, which has the same surface: `base` is what a RESULT is built on and united with.  The walk costs about 32 calls of the part at every
        # point it is read, and a result that held it in every term carried that cost into every cell of its drawing -- the result of such a part did not finish drawing in twenty minutes.  The remedies still READ
        # the part through `field`, the walk, for its distances; only the field they are united with is plain.  For any other part, and for one that was rescaled below, `base` is `field` itself)
        shown = getattr(self.field, '_display_field', None)
        if abs(self.slope - 1.0) > 0.05 and 0.02 < self.slope < 50.0:
            self.field = self.field * (1.0 / self.slope)
            shown = None
        self.base = Shape.wrap(shown) if shown is not None else self.field
        # (the step of the normals, a thousandth of the part, and NOT a round number: the normal is the mean of the derivative asked at p - h and p + h, and a plane of the part's surface that p - h or p + h
        # lies on exactly is where the derivative is the mean of the rules' on both sides, half a face's -- a sheet of nothing in the field, at the distance h from every face.  Anything that samples the field
        # in steps that divide h (a tape every 0.0005 mm, a grid of round numbers) hits it: a wall of 0.30 measured 0.344 where h was 0.023.  h that is no multiple of a round number is never on it)
        self.step = max(1e-3 * self.size, 0.004) * _ODD_STEP
        self.tie = max(5e-7 * self.size, 2e-6)         # (how far apart the lookups that are combined are kept in value: see _join)
        # (where the print plate is, from the part: a check of what was made keeps the part's, not the made part's padded box -- which the result carries, for a check a user runs on it)
        self.plate_box = (tuple(float(c) for c in carried[0]), tuple(float(c) for c in carried[1])) if carried else (self.lo, self.hi)
        self._normals = None
        self._one_sided = {}
        self.clamp_calls = self._expensive_part()

    def _expensive_part(self):
        ''' Is a point of the part's field expensive (more than 15 microseconds)?  The remedies make calls of the part at points built from its own value and normal; where the part's interval is loose in a cell (a topology-optimized part
            held in a mesh) the box of such a call is as wide as that, and holding the offsets (manufacturing._clip) lets the mesher shorten the part for the call.  For a part of boxes and cylinders, whose interval is tight, the held offsets
            only add clauses -- and the drawing of the CNC sampler's plate went from 55 s and 1 GB to out of memory.  FIELDES_CLIP=1 / FIELDES_NO_CLIP=1 force it, for tests '''
        self.point_us = 0.0
        if os.environ.get('FIELDES_NO_CLIP'):
            return False
        if os.environ.get('FIELDES_CLIP'):
            return True
        # (the cost that matters is a point near the surface, where the mesher works -- a random point of the box is mostly far from it, where an oracle is cheap: the topology bracket costs 27 us a point
        # over its box and 100 to 200 near its surface.  3000 points thrown at random; those within 3 % of the part's size of the surface are timed again, when there are 80 of them or more)
        rng = random.Random(5)
        pts = [tuple(rng.uniform(self.lo[i], self.hi[i]) for i in range(3)) for _ in range(3000)]
        _values(self.field, pts[:50])
        started = time.time()
        vals = _values(self.field, pts)
        self.point_us = 1e6 * (time.time() - started) / len(pts)
        near = [p for p, v in zip(pts, vals) if abs(v) <= 0.03 * self.size]
        if len(near) >= 80:
            started = time.time()
            _values(self.field, near)
            self.point_us = 1e6 * (time.time() - started) / len(near)
        return self.point_us > 15.0         # (measured the same way: the plate of the CNC sampler 1.0 us, the topology bracket 27 to 30 us)

    @property
    def normals(self):
        if self._normals is None:
            self._normals = _Normals(self.field, self.step)
        return self._normals

    def one_sided_normals(self, kind):
        ''' the normals by forward or backward differences ('forward' / 'backward'): see _Normals (what the middle of a gap is read with) '''
        if kind not in self._one_sided:
            self._one_sided[kind] = _Normals(self.field, self.step, kind)
        return self._one_sided[kind]


# ---------------------------------------------------------------------------
# Measuring: a few thousand points of the surface, to report how much breaks a rule and where.  (A measurement: what is found here is
# said in numbers and is never drawn or built from)

def _values(field, points):
    ''' A field's value at points ((x, y, z) each), the library's batch evaluation '''
    from fieldes.stdlib.exporting import _eval_at
    if not points:
        return []
    return _eval_at(field, [c for p in points for c in p], len(points))


def _typical_slope(field, lo, hi, size, count=500, seed=11):
    ''' The slope of the field (the length of its gradient) at its surface, as the middle of what is found at the points of 500 thrown at random that lie within 3 % of the part's size of the
        surface (60 of them are read by central differences): 1 for a distance, 2 for a distance times 2, 0.5 for a part that was scaled up by 2.  1 where too few lie near the surface to say '''
    rng = random.Random(seed)
    cloud = [tuple(rng.uniform(lo[i], hi[i]) for i in range(3)) for _ in range(count)]
    near = [p for p, v in zip(cloud, _values(field, cloud)) if abs(v) <= 0.03 * size][:60]
    if len(near) < 12:
        return 1.0
    h = max(1e-3 * size, 0.004)
    probes = []
    for p in near:
        probes.extend(((p[0] + h, p[1], p[2]), (p[0] - h, p[1], p[2]), (p[0], p[1] + h, p[2]), (p[0], p[1] - h, p[2]), (p[0], p[1], p[2] + h), (p[0], p[1], p[2] - h)))
    v = _values(field, probes)
    slopes = []
    for i in range(len(near)):
        gx, gy, gz = (v[6 * i] - v[6 * i + 1]) / (2 * h), (v[6 * i + 2] - v[6 * i + 3]) / (2 * h), (v[6 * i + 4] - v[6 * i + 5]) / (2 * h)
        slopes.append(math.sqrt(gx * gx + gy * gy + gz * gz))
    slopes.sort()
    return slopes[len(slopes) // 2]


def _where(p):
    return '(%.4g, %.4g, %.4g)' % p


def _remembered(name, limit=6):
    ''' A call that was made before -- the same part (its expression, and the numbers it has), the same constraints and settings: a script run again after a model was shown or
        hidden, after a change to what comes after it -- gives back what it made, and does not measure and shape the part again (seconds to minutes).  Like every content
        cache (content_cache.py) it keeps the very object a miss made, in this session.  A part, a constraint or a setting that has no content key is made every time. '''
    def deco(fn):
        cache = cache_for(name, limit)

        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            try:
                part = args[0] if args else kwargs.get('part')
                key = (value_key(args), value_key(kwargs), value_key([tuple(x) for x in locks_of(part)]), value_key(getattr(part, '_free', None)))
            except Uncacheable:
                return fn(*args, **kwargs)
            hit, value = cache.get(key)
            if not hit:
                value = fn(*args, **kwargs)
                cache.put(key, value)
            return value
        wrapper.cache = cache
        return wrapper
    return deco

def _extent_along(ctx, d):
    return sum(abs(d[i]) * (ctx.hi[i] - ctx.lo[i]) for i in range(3)) + 1e-6
