'''
Manufacturing, reading a part's field: lookups of the field at moved points, its normals by central differences, its values at many points, the box the part is in.

These are the tools both the check (manufacturing_check.py) and the remedies (manufacturing.py) read a part with; there is no rule in them, and nothing here says
what is broken or how to fix it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import functools
import math

from fieldes.shape import Shape
from fieldes.stdlib.content_cache import cache_for, value_key, Uncacheable
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


# ---------------------------------------------------------------------------
# Measuring: a few thousand points of the surface, to report how much breaks a rule and where.  (A measurement: what is found here is
# said in numbers and is never drawn or built from)

def _values(field, points):
    ''' A field's value at points ((x, y, z) each), the library's batch evaluation '''
    from fieldes.stdlib.exporting import _eval_at
    if not points:
        return []
    return _eval_at(field, [c for p in points for c in p], len(points))


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
