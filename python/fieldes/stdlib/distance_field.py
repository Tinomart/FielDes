'''
Distances and rolling balls, read from a field itself: nothing is meshed, nothing is sampled onto a grid.

A shape's field is only a distance where it was built to be one (a sphere, a plane, the primitives): the field of a box is the largest of the
distances to its six planes, which is the distance to the box only where one plane is the nearest thing, and the field of a part made by booleans, of an imported
part, of a result, rises faster here and slower there.  What offsets, shells, rounded edges and fillets mean is a DISTANCE, to the surface or from a ball's centre.
It is found from the field's value and gradient.  The gradient is the field's SYMBOLIC derivative (`partials`: the kernel derives the field's expression by the rules of calculus, so that
the intervals the mesher makes of it are the derivative's own -- over a flat face, one number); for a field with a native field in it (a mesh's distance) there is no formula, and the
gradient is read by central differences of calls.  From a point p the surface lies along the gradient, f(p) / |grad f| away: that is the first step of a walk to the surface,
ending at a point q1 where the field is asked again.  A second step from there, along the gradient at q1, ends at the foot of the perpendicular when the two faces that
met are at right angles (an edge of a box, an inside corner of an L), and the length of the walk -- sqrt(t1^2 + t2^2 + 2 t1 t2 (n1 . n2)) from the two steps and their directions --
is the distance.  Nothing else is asked of the field: the whole is 7 calls at one level of nesting (a call whose arguments hold calls: no deeper, a third step took the mesher to
8 GB on a scene of a few hundred cells, when the gradient was still read by differences).

    exact for a distance already (one step), for a sphere, a cylinder and a plane, for the edge of a box and the inside corner of an L (two steps: the faces are at right angles),
    approximate where three faces meet (a corner of a box: the walk stops after two of them) and where two meet at another angle than a right one (the second step
    is taken along the gradient of the point it ends at, so the angle is read, and the length is a little off), and left alone -- the shape's own field -- where the field
    has no slope (a plateau: the field of a block cut by a cutter that reaches out beyond it, above the cut: no direction to walk in)

The three constructions made of it:  `distance` (the signed distance to the surface), `closing` (a rolling ball of radius r in the air: it rounds the inside
edges and corners, fills what is narrower than the ball) and `opening` (the same ball in the material: it rounds the outside edges and corners, removes what is
thinner than the ball).  A ball's centre lies where the field is r from the surface; the closing is everything the ball cannot reach from the air side, so its
surface is the points that are a ball's radius from the set of centres.  A walk that turns back on itself (the second step runs against the first: the two faces
face each other, a slot or a wall narrower than two radii) finds no centre: the ball does not fit, and says so.

Each is made in a band round the surface -- `reach` -- and is the shape's own field outside it: there the walk is not made, and a mesher that asks the field
over a cell outside the band skips the walk (the same limit by min and max as the remedies of manufacturing.py use).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
from fieldes.shape import Shape

_TOP = 1.5                # (the most the band lets the construction change the field by, in units of the reach: what fills a slot all the way, or removes a fin)
_SLOPE = (0.3, 0.5)       # (a slope of the field under the first and above the second is no slope to walk by: the walk is not made there, and over the ramp between it is made in part)
_SHIFT = (0.62, 0.49, 0.61)   # (the direction, in units of the step, in which the derivative is asked ahead of a point: no axis, and not square to a plane that a part has by habit)


def _coordinates():
    return Shape.X(), Shape.Y(), Shape.Z()


def partials(f):
    ''' The partial derivatives of the field (fx, fy, fz) as fields of their own -- the symbolic derivative, by the rules of calculus (Shape.partial) -- or None where the field has a native
        field in it (a mesh's distance, noise: no formula).  Made once for a field and kept on it.  They replace the differences of calls that a gradient used to be read by: an interval over
        a cell of the mesher's is as many times too wide as the cell is wider than the step of a difference (a thousandth of the part: 12 times at res 3), every bound made from it was loose
        and the mesher refined the whole band round the surface (an L of 20 mm took 8 GB; the remedies of manufacturing.py drew 4 times slower); the derivative's intervals are the
        derivative's own -- one number over a flat face '''
    cached = getattr(f, '_partials_cache', False)
    if cached is not False:
        return cached
    # (a field that is made to have the surface of another -- exact_distance -- has the other's derivative near that surface: it is what the field's gradient is, and the derivative of the walk itself, calls
    # inside calls, is several times the cost of the field for every level and was the memory of the tools that read normals from it)
    hint = getattr(f, '_partials_hint', None)
    parts = hint if hint is not None else (tuple(f.partial(a) for a in range(3)) if hasattr(f, 'partial') else (None, None, None))
    out = parts if all(p is not None for p in parts) else None
    try:
        f._partials_cache = out
    except Exception:
        pass
    return out


def _gradient(f, parts, qx, qy, qz, h, behind=False):
    ''' The gradient of `f` at the point (qx, qy, qz), as three fields: each partial is its derivative asked a step AHEAD of the point along its own axis (`behind`: a step behind it) -- a call keeps
        the field where it is and asks it at the place.  Not asked AT the point: a surface, an edge, the middle of a slot is where the field's rule changes, and a derivative asked exactly there is the
        mean of both rules' (half of a face's, since a sqrt of 0 has none, and nothing on the middle of a slot, where the walk would have no direction: a seam of air or material is left); a point on a
        flat face is on it exactly, in the arithmetic of the mesher's grid, whenever the face lies on a round number.  A step away, the derivative is one rule's: a face's gradient, whole.  For a field
        with a native field in it, central differences of calls over `h` '''
    if parts is not None:
        # (all three partials at ONE shifted point, along a direction that is none of the axes and none of the usual planes: on the bisector of a wedge, one step along x reads the branch of y and one step
        # along y the branch of x, and the gradient put together from them is 0)
        s = -h if behind else h
        px, py, pz = qx + s * _SHIFT[0], qy + s * _SHIFT[1], qz + s * _SHIFT[2]
        return parts[0](px, py, pz), parts[1](px, py, pz), parts[2](px, py, pz)
    k = 0.5 / h
    return ((f(qx + h, qy, qz) - f(qx - h, qy, qz)) * k,
            (f(qx, qy + h, qz) - f(qx, qy - h, qz)) * k,
            (f(qx, qy, qz + h) - f(qx, qy, qz - h)) * k)


def _step(f, parts, qx, qy, qz, value, level, h, cap):
    ''' One Newton step from the point (qx, qy, qz), where the field is `value`, towards where it is `level`: the unit vector of the gradient there, the signed length of the step along it,
        and the size of the gradient '''
    gx, gy, gz = _gradient(f, parts, qx, qy, qz, h)
    g = (gx * gx + gy * gy + gz * gz + 1e-12).sqrt()
    t = ((level - value) / g).max(-cap).min(cap)
    return gx / g, gy / g, gz / g, t, g


def _sign(v, eps):
    ''' -1 below 0, 1 above, a smooth step through 0 over `eps` (the distance is 0 there too, so the product is smooth) '''
    return v / (v * v + eps * eps).sqrt()


def _foot_real(f, parts, qx, qy, qz, value, h, cap):
    ''' 1 where `f` is a distance at the point (qx, qy, qz), where it is `value`, and 0 where it is not.  A field that is a distance has this property: from a point, along the gradient, by the field's value over
        the size of the gradient, the walk ends ON the surface (the field is 0 there).  The field of `difference(box, cutter)` where the cutter reaches out beyond the box is a distance at the faces it has cut,
        and above the cut it is the distance to the cutter's faces carried up beyond the box: walls that are not there (the field is not 0 on them) -- the top of the cutter is one, a ceiling over the pocket --,
        and a ball that "does not fit" between them is not a ball that does not fit between the part's.  Tried at both ends of the gradient (a step ahead and a step behind), so that at an edge, on the middle of a
        slot -- where the two ends read the two faces -- one face's foot is enough.  Only for a field with a derivative (for one with a native field in it, a mesh's distance, the field is a distance by what it is
        made of). '''
    if parts is None:
        return 1.0
    tol = 0.02 * cap
    best = None
    for behind in (False, True):
        gx, gy, gz = _gradient(f, parts, qx, qy, qz, h, behind)
        g = (gx * gx + gy * gy + gz * gz + 1e-12).sqrt()
        t = ((-value) / g).max(-cap).min(cap)
        foot = f(qx + t * gx / g, qy + t * gy / g, qz + t * gz / g)
        real = (2.0 - foot.abs() * (1.0 / tol)).max(0).min(1)
        best = real if best is None else best.max(real)
    return best


def _trusted(f, parts, qx, qy, qz, value, t, h, cap):
    ''' How far a step of length `t` from the point (qx, qy, qz) is to be believed: the face it leads to must be there (`_foot_real`), but only as far as the step is a step -- one of a few hundredths of a
        millimetre changes nothing, and is believed whatever it leads to '''
    if parts is None:
        return 1.0
    size = (t.abs() * (1.0 / (0.03 * cap))).min(1)
    return 1.0 - size * (1.0 - _foot_real(f, parts, qx, qy, qz, value, h, cap))


def _against(cosine, t, scale):
    ''' 1 where a step ran against the one before it (the cosine between them under -0.5) and was a step of some size: the walk crossed a gap or a wall between two faces that face each other '''
    against = ((-cosine - 0.5) * 2.0).max(0).min(1)
    size = (t * t + 1e-18).sqrt() / (0.05 * scale + 1e-9)
    return against * size.min(1)


def _walk(f, parts, level, h, cap, hair, radius=None):
    ''' From every point towards where `f` equals `level`, in three steps: (the length of the walk, 1 where it turned back on itself, how far the field has a slope to walk by and is a distance
        there -- 1 where it has, 0 where it has none).  A step is a Newton step along the gradient to where the field is `level`.  The walk ends on the surface of the faces that are near: on one face
        after one step (a plane, a cylinder, a sphere), on the edge of two after two, at the corner of three after three.  `radius` (the ball's, when there is one) is what a step is measured against to
        tell a step of some size: a step that runs against the one before it, across a gap or a wall between two faces that face each other, finds no place for a ball -- the walk "turned back" '''
    x, y, z = _coordinates()
    n1x, n1y, n1z, t1, g1 = _step(f, parts, x, y, z, f, level, h, cap)
    q1x, q1y, q1z = x + t1 * n1x, y + t1 * n1y, z + t1 * n1z
    f1 = f(q1x, q1y, q1z)
    n2x, n2y, n2z, t2, g2 = _step(f, parts, q1x, q1y, q1z, f1, level, h, cap)
    q2x, q2y, q2z = q1x + t2 * n2x, q1y + t2 * n2y, q1z + t2 * n2z
    f2 = f(q2x, q2y, q2z)
    n3x, n3y, n3z, t3, g3 = _step(f, parts, q2x, q2y, q2z, f2, level, h, cap)
    c12 = n1x * n2x + n1y * n2y + n1z * n2z
    c23 = n2x * n3x + n2y * n3y + n2z * n3z
    c13 = n1x * n3x + n1y * n3y + n1z * n3z
    # (the length of the walk.  Along a smooth surface the later steps only refine the first: the steps are one after the other, the length is that of the path they make.  Where two planes meet in
    # an edge -- the first step ends on one of them, the second on the other -- the point is at the edge's nearest point, and its distance to the edge LINE is sqrt(t1^2 + t2^2 / sin^2), sin being that
    # of the angle between the normals: what two steps along them are for faces at right angles only.  The edge's way is taken from an angle of 33 degrees between the normals, the path's below 14,
    # and the two are mixed between.  A third step is another face when its direction is a NEW one (not the first face's again, which is what the third step of two faces at an angle is: back
    # to the first) and a step of some size: then the point is at a corner, and the length is that of the path of the three, which is exact when the three faces are at right angles to each other.
    # With a hair under the root: where the walk ends where it began -- on the surface, which is where the mesher places its vertices -- a root of 0 has an infinite slope, and the mesher reads
    # the slope there)
    path2 = (t1 * t1 + t2 * t2 + 2.0 * t1 * t2 * c12 + hair * hair).sqrt()
    edge = (t1 * t1 + t2 * t2 / (1.0 - c12 * c12).max(0.0625) + hair * hair).sqrt()
    crease = ((1.0 - c12 - 0.03) * (1.0 / 0.13)).max(0).min(1)
    len2 = path2 + crease * (edge - path2)
    path3 = (t1 * t1 + t2 * t2 + t3 * t3 + 2.0 * (t1 * t2 * c12 + t2 * t3 * c23 + t1 * t3 * c13) + hair * hair).sqrt()
    low, high = _SLOPE
    gate1 = ((g1 - low) / (high - low)).max(0).min(1)
    gate2 = ((g2 - low) / (high - low)).max(0).min(1)
    gate3 = ((g3 - low) / (high - low)).max(0).min(1)
    new = ((1.0 - c13.abs() - 0.05) * (1.0 / 0.2)).max(0).min(1)
    corner = ((t3.abs() - 0.02 * cap) * (1.0 / (0.05 * cap))).max(0).min(1) * new * gate3
    length = len2 + corner * (path3 - len2) - hair
    # (every step is believed as far as the face it leads to is there: see _trusted)
    trust = (_trusted(f, parts, x, y, z, f, t1, h, cap) * _trusted(f, parts, q1x, q1y, q1z, f1, t2, h, cap) *
             _trusted(f, parts, q2x, q2y, q2z, f2, t3, h, cap))
    has_slope = gate1 * gate2 * trust
    turned = 0.0
    if radius is not None:
        turned = _against(c12, t2, radius).max(_against(c23, t3, radius))
    return length, turned, has_slope


_STEP = 0.1               # (the step of the differences, in units of the reach: see _scales)


def _scales(size, reach, exact=True):
    ''' (the step of the gradient, the width of the smooth sign, the hair under a root, the "far" of a ball that does not fit).  With the field's derivative (`exact`) the step only has to be off the
        surface and the edges, where the derivative is the mean of the rules' (a thousandth of the size).  For a field with a native field in it the gradient is a difference of calls, and the step
        is a tenth of the reach: over a cell of the mesher's, the interval of f(x + h) - f(x - h) is as wide as the cell's two calls together, so that divided by 2h the interval of the gradient is
        as many times too wide as the cell is wider than the step; with a step of a thousandth of the size that is 12 times at a cell of a third of a millimetre, every bound after it is loose, the
        mesher cannot rule out a cell of the band and refines the whole of it (8 GB for an L of 20 mm).  With a step of a tenth of the reach the gradient is the one of the surface a step wide, which
        is the faces' own where they are flat, and at an edge is blurred over one step either side of the bisector '''
    h = max(1e-3 * size, 1e-9) if exact else max(_STEP * reach, 1e-3 * size, 1e-9)
    return h, max(1e-4 * size, 1e-10), max(1e-6 * size, 1e-12), max(1e3, 1e3 * size)


def _gate(f, scale):
    ''' How much of the construction is needed at each point: 0 where the surface is flat for as far as `scale` reaches, up to 1 where it is not.  The second difference of the field at that
        scale along an axis, f(p + s e) + f(p - s e) - 2 f(p), is exactly 0 where f is linear along it (the distance to a plane is, on either side of it), and is not where a kink -- the edge of a
        box, the bisector of a wedge -- or a curve tighter than the scale lies within the scale: it is the three of them added, over the scale.  It is made of six calls with the scale for a step, not
        a thousandth of the part, so its interval over a cell of the mesher's is narrow (the cell's width over the scale), and a cell where it is 0 is a cell where the construction changes nothing,
        which the mesher can tell without working it through: only the tubes round the edges are refined, and not the whole band round the surface '''
    x, y, z = _coordinates()
    total = None
    for ex, ey, ez in ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)):
        second = (f(x + ex * scale, y + ey * scale, z + ez * scale) + f(x - ex * scale, y - ey * scale, z - ez * scale) - 2.0 * f).abs()
        total = second if total is None else total + second
    return ((total * (1.0 / scale) - 0.05) * (1.0 / 0.25)).max(0).min(1)


def _band(f, new, reach, top, gate):
    ''' `new` where the surface of `f` is within `reach` and the gate is open, and `f` itself beyond: the change from `f` to `new` is held between -cap and cap, the cap is 0 from `reach` out and
        where the gate is shut.  A cell that lies wholly beyond the band, or where the gate is shut, has a cap of exactly 0, so the change is exactly 0 and the mesher drops the walk from the tape
        there, where it would otherwise have to work through it in every cell of the scene '''
    cap = ((reach - f.abs()) * (10.0 * top / reach)).max(0).min(gate * top)
    return f + (new - f).min(cap).max(-cap)


def distance(f, size, reach):
    ''' The signed distance from every point to the surface of `f` (negative inside), where the surface is within `reach`; `f` itself beyond.  `size` is the part's size (mm) '''
    parts = partials(f)
    h, eps, hair, _ = _scales(size, reach, parts is not None)
    length, _turned, has_slope = _walk(f, parts, 0.0, h, _TOP * reach, hair)
    return _band(f, f + has_slope * (length * _sign(f, eps) - f), reach, _TOP * reach, _gate(f, reach))


def closing(f, radius, size, reach):
    ''' `f` closed by a ball of `radius` (a number, or a field), rolling in the air: the inside edges and corners are rounded with that radius, and what is narrower than the ball
        (a slot, a gap) is filled.  `reach` is the largest radius.  The surface of the closing is the set of points a `radius` from the points that the ball's centre can be at -- where
        `f` is `radius` or more -- so `radius + (the distance to that set, signed)` is the field.  A point whose walk to the centres turns back on itself is far from them (the ball does
        not fit), and is material '''
    parts = partials(f)
    h, eps, hair, big = _scales(size, reach, parts is not None)
    length, turned, has_slope = _walk(f, parts, radius, h, _TOP * reach, hair, radius)
    out = radius + length * _sign(f - radius, eps) - big * turned
    return _band(f, f + has_slope * (out - f), 1.05 * reach, _TOP * reach, _gate(f, reach))


def opening(f, radius, size, reach):
    ''' `f` opened by a ball of `radius` (a number, or a field), rolling in the material: the outside edges and corners are rounded with that radius, and what is thinner than the ball
        (a fin, a thin wall) is removed.  The mirror of `closing`: the centres are where `f` is `-radius` or less '''
    parts = partials(f)
    h, eps, hair, big = _scales(size, reach, parts is not None)
    length, turned, has_slope = _walk(f, parts, -radius, h, _TOP * reach, hair, radius)
    out = length * _sign(f + radius, eps) - radius + big * turned
    return _band(f, f + has_slope * (out - f), 1.05 * reach, _TOP * reach, _gate(f, reach))
