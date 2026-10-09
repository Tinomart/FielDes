'''
Boundary conditions you can see.

    from fieldes import *

    part = ...
    base = fixed(base_face)                                       # held here
    push = force(lug_face, (0, -2000, 0))                         # pushed here
    result = static_analysis(part, supports=[base], loads=[push, gravity()], material=aluminium, element_size=4)
    base                                                          # displayed: the held surface, with pads
    push                                                          # displayed: the loaded surface, with arrows

Every boundary condition -- a support, a force, a temperature, an inlet ... -- is a model of its own: a row of the model tree with an eye, and
it is drawn the way structural and flow analysis programs draw it, on the body of the simulation it was given to (the body itself is drawn by
its own row: show it with its eye to see both):

    * the surface it acts on is tinted, in a colour that says what it is (blue: fixed support, cyan: sliding support, red: force, orange:
      gravity, yellow: heat in, green: inlet, violet: outlet, grey: wall, light blue: slip, ...)
    * a force, an inlet, an outlet or a heat input is an ARRAY of identical arrows over the surface, each touching it with its tip when it pushes
      in and with its tail when it points out, with what it says written beside it (the total force, the speed, the pressure, the power)
    * a support, a wall, a slip, a temperature or a convection is an array of flat pads lying on the surface, with its name beside it
    * gravity is one arrow beside the part along the acceleration, with its value

A simulation has the toggle `boundary conditions` in the model tree: it shows or hides all of its conditions at once.

The arrows, pads and texts are not part of the meshed model: the viewport draws them over it, with a size that follows
the zoom, so they are never cut off by the render region or made ragged by the render's resolution.

The tint is the body's own surface, coloured and drawn alone (a hair towards the eye, so that it wins over the body where both are shown); a
place of the body counts as in a region when it is within about 1 % of the body's size of it.  A condition that no simulation has been given
yet is drawn on a body all the same -- the one its surface was picked on (select_surface, surface_from_bodies), else the biggest solid of the
script that its region reaches -- so it looks the same while you are setting a simulation up; only when the script has no body at all is it
drawn as the region itself (the place it acts).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import inspect
import math
import weakref

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib.content_cache import content_cached
from fieldes.stdlib.fea import _Support, _Force, _Gravity, _Thermal, _bounds, FeaError
from fieldes.stdlib.fields import evaluate

__all__ = []

# What a colour of the picture means (the colour map 'bc' of the application: the value is the category; see colormap.hpp)
FIXED, SLIDING, FORCE, GRAVITY, HEAT, SELECTED = 1, 2, 3, 4, 5, 6
INLET, OUTLET, WALL, SLIP, TEMPERATURE, CONVECTION, GENERATION = 7, 8, 9, 10, 11, 12, 13
_NAMES = {FIXED: 'Fixed support', SLIDING: 'Sliding support', FORCE: 'Force', GRAVITY: 'Gravity', HEAT: 'Heat in', INLET: 'Inlet',
          OUTLET: 'Outlet', WALL: 'Wall', SLIP: 'Slip', TEMPERATURE: 'Fixed temperature', CONVECTION: 'Convection',
          GENERATION: 'Heat generated'}
CATEGORIES = 13
_RANGE = (0.0, float(CATEGORIES))

# How many symbols a loaded or supported face gets (about), and how big they may be, as shares of the part's size
_FORCE_ARROWS = 48
_SUPPORT_PADS = 24
_MAX_SYMBOLS = 90


def _kind_of(item):
    ''' How a condition is drawn: (category, 'pad' | 'in' | 'out' | 'none', the text beside it) -- a pad lies on the surface, an arrow goes in
        (its tip on the surface) or out (its tail on it), 'none' is only the tint '''
    from fieldes.stdlib import thermal, fluid
    if isinstance(item, thermal._Temperature):
        value = item.value
        return TEMPERATURE, 'pad', ('%g °C' % value) if isinstance(value, (int, float)) else 'Temperature (a field)'
    if isinstance(item, thermal._HeatGeneration):
        return GENERATION, 'none', '%g W generated' % item.power
    if isinstance(item, thermal._Heat):
        return HEAT, 'in' if item.power >= 0 else 'out', ('%g W' % item.power) if item.power >= 0 else ('%g W out' % -item.power)
    if isinstance(item, thermal._Convection):
        h = item.coefficient
        return CONVECTION, 'pad', ('Convection h = %g' % h) if isinstance(h, (int, float)) else 'Convection (a field)'
    if isinstance(item, fluid._Inlet):
        if item.flow_rate > 0:
            what = '%g mm³/s' % item.flow_rate
        elif item.speed > 0:
            what = '%g mm/s' % item.speed
        else:
            what = '%g mm/s' % math.sqrt(sum(v * v for v in item.direction))
        return INLET, 'in', 'Inlet ' + what
    if isinstance(item, fluid._Outlet):
        return OUTLET, 'out', 'Outlet' if not item.pressure else 'Outlet %g MPa' % item.pressure
    if isinstance(item, fluid._Wall):
        moving = any(abs(v) > 0 for v in item.velocity)
        return WALL, 'pad', 'Moving wall' if moving else 'Wall'
    if isinstance(item, fluid._Slip):
        return SLIP, 'pad', 'Slip'
    return None


_DRAWN = weakref.WeakKeyDictionary()          # (condition -> {body: what was drawn}: not kept in the condition, which is hashed by what it holds)


def display_condition(item):
    ''' What a condition shows for itself (the way its model is displayed): on the body of the simulation it was given to, or on the body its
        surface was picked on, drawn once for each body '''
    from fieldes.stdlib.fea import _PARTS
    parts = _PARTS.get(item)
    part = parts[0] if parts else _body_for([item])
    drawn = _DRAWN.setdefault(item, {})
    key = id(part)
    if key not in drawn:
        drawn[key] = _draw([item], part)
    return drawn[key]


def describe(item):
    ''' One line that says what a condition is (its text beside it in the picture) '''
    if isinstance(item, _Support):
        axes = ''.join(a for a, on in zip('xyz', item.axes) if on)
        return 'fixed ({})'.format(axes) if len(axes) == 3 else 'sliding support (fixed in {})'.format(axes or 'nothing')
    if isinstance(item, _Force):
        n = math.sqrt(sum(c * c for c in item.vector))
        return 'force {:g} N ({:g}, {:g}, {:g})'.format(n, *item.vector)
    if isinstance(item, _Gravity):
        return 'gravity ({:g}, {:g}, {:g}) mm/s2'.format(*item.g)
    if isinstance(item, _Thermal):
        return 'thermal expansion (reference {:g})'.format(item.reference)
    kind = _kind_of(item)
    return kind[2] if kind else type(item).__name__


def _regions_of(items):
    ''' Every region a list of conditions acts in '''
    return [i.region for i in items if getattr(i, 'region', None) is not None]


def _members(region):
    ''' The regions a united region is made of (see fea._united), or itself '''
    inner = getattr(region, '_members', None)
    return [m for r in inner for m in _members(r)] if inner else [region]


def _bodies_overlap(a, b):
    return all(a[0][i] <= b[1][i] and b[0][i] <= a[1][i] for i in range(3))


def _body_for(conditions):
    ''' The body that conditions no simulation has been given are drawn on, as they are on the body of the simulation that is: the body a
        surface of theirs was picked on (select_surface, surface_from_bodies), else the biggest solid of the script that their regions
        reach (the script's own models: nothing it only made on the way), else None -- the script has no body to draw them on '''
    from fieldes.stdlib.selection import SurfaceSelection
    regions = _regions_of(conditions)
    for r in (m for region in regions for m in _members(region)):
        if isinstance(r, SurfaceSelection) and isinstance(getattr(r, 'shape', None), Shape):
            return r.shape
    try:
        from fieldes import runner
        from fieldes.kinds import kind_of
        models = list(runner.last_globals.values())
    except Exception:
        return None
    reach = [b for b in (getattr(r, '_bounds', None) for r in regions) if b]
    skip = {id(r) for r in regions}
    best, biggest = None, 0.0
    for v in models[:400]:
        if not isinstance(v, Shape) or id(v) in skip or kind_of(v) != 'solid':
            continue
        box = getattr(v, '_bounds', None)
        if box is None:
            try:
                box = _bounds(v)
            except Exception:
                continue
        if not all(_bodies_overlap(box, b) for b in reach):
            continue
        volume = 1.0
        for i in range(3):
            volume *= max(box[1][i] - box[0][i], 1e-9)
        if volume > biggest:
            best, biggest = v, volume
    return best


@content_cached('bc_support_symbols', limit=48)
def _support_symbols(part, region, lo, hi, size, tol):
    ''' Where the pads of a support go (see _symbol_points), remembered by the part, the region and the numbers: the script runs again on every
        edit -- a hide, a show -- and finding them evaluates the part at thousands of points '''
    return _within_cap(_symbol_points, part, region, lo, hi, size, tol, want=_SUPPORT_PADS)


@content_cached('bc_force_symbols', limit=48)
def _force_symbols(part, region, lo, hi, size, tol, d):
    ''' Where the arrows of a force along `d` go (see _force_points), remembered the same way '''
    return _within_cap(_force_points, part, region, lo, hi, size, tol, d, want=_FORCE_ARROWS)


def _draw(items, part):
    ''' What the viewport shows of conditions: on `part` -- the body's own surface tinted where it is held, loaded or otherwise acted on, the
        arrows and pads -- or, with no body to draw them on, the regions themselves '''
    supports = [i for i in items if isinstance(i, _Support)]
    drawn_loads = [i for i in items if isinstance(i, (_Force, _Gravity, _Thermal))]
    others = [i for i in items if not isinstance(i, (_Support, _Force, _Gravity, _Thermal))]
    if part is None:
        return _draw_regions(items)
    try:
        lo, hi = _bounds(part)
    except FeaError:
        lo, hi = (-50.0, -50.0, -50.0), (50.0, 50.0, 50.0)
    size = max(hi[i] - lo[i] for i in range(3)) or 1.0
    lift = 0.002 * size             # the symbols stand this far off the surface
    tol = 0.012 * size              # a place counts as inside a region within this of it

    pieces = []                     # (category, field that is < 0 inside it, tolerance): the tint
    glyphs = []                     # (kind, tip at the point, point, direction, size): what the viewport draws
    labels = []                     # (kind, text, point)
    notes = []
    present = set()

    for s in supports:
        cat = FIXED if all(s.axes) else SLIDING
        pieces.append((cat, s.region, tol))
        present.add(cat)
        pts, step = _support_symbols(part, s.region, tuple(lo), tuple(hi), size, tol)
        if not pts:
            notes.append('a support is where the part is not')
            continue
        for p, n in pts:
            glyphs.append((cat, False, tuple(p[a] + n[a] * lift for a in range(3)), n, 0.7 * step))
        axes = ''.join(a for a, on in zip('xyz', s.axes) if on)
        text = 'Fixed' if cat == FIXED else 'Sliding (fixed in {})'.format(axes or 'nothing')
        labels.append((cat, text, _label_point(pts, 0.9 * step)))

    forces = [l for l in drawn_loads if isinstance(l, _Force)]
    biggest = max([math.sqrt(sum(c * c for c in l.vector)) for l in forces] or [1.0]) or 1.0
    for l in drawn_loads:
        if isinstance(l, _Force):
            pieces.append((FORCE, l.region, tol))
            present.add(FORCE)
            n = math.sqrt(sum(c * c for c in l.vector))
            if n <= 0:
                continue
            d = _unit(l.vector)
            pts, step = _force_symbols(part, l.region, tuple(lo), tuple(hi), size, tol, tuple(d))
            if not pts:
                notes.append('a force acts where the part is not')
                continue
            length = min(0.14 * size, max(0.03 * size, 1.3 * step)) * (0.75 + 0.25 * n / biggest)
            far = []                                     # the end of each arrow that is away from the surface
            for p, nrm, room in pts:
                dn = _dot(d, nrm)
                base = tuple(p[a] + nrm[a] * lift for a in range(3))
                ln = length if room is None else min(length, 0.85 * room)       # (an arrow in a bore is as long as the bore leaves room for)
                if dn < 0.0:                             # pushing in: the tip touches the surface
                    glyphs.append((FORCE, True, base, d, ln))
                    far.append((tuple(base[a] - d[a] * ln for a in range(3)), nrm))
                else:                                    # pulling out: the tail is on the surface
                    glyphs.append((FORCE, False, base, d, ln))
                    far.append((tuple(base[a] + d[a] * ln for a in range(3)), nrm))
            labels.append((FORCE, '{:g} N'.format(float('{:.4g}'.format(n))), _label_point(far, 0.35 * length)))
        elif isinstance(l, _Gravity):
            arrow = _gravity_arrow(l.g, lo, hi, size)
            if arrow is not None:
                p, d, length = arrow
                glyphs.append((GRAVITY, False, p, d, length))
                g = math.sqrt(sum(c * c for c in l.g))
                labels.append((GRAVITY, 'Gravity {:.4g} m/s²'.format(g / 1000.0),
                               tuple(p[i] - d[i] * 0.25 * length for i in range(3))))
                present.add(GRAVITY)

    # The other kinds -- a temperature, a heat, a convection, an inlet, an outlet, a wall, a slip -- as `_kind_of` says: a tint, and pads or arrows
    for item in others:
        kind = _kind_of(item)
        if kind is None:
            continue
        cat, how, text = kind
        pieces.append((cat, item.region, tol))
        present.add(cat)
        if how == 'none':
            continue
        pts, step = _support_symbols(part, item.region, tuple(lo), tuple(hi), size, tol)
        if not pts:
            notes.append('%s acts where the part is not' % _NAMES.get(cat, 'a condition').lower())
            continue
        if how == 'pad':
            for p, n in pts:
                glyphs.append((cat, False, tuple(p[a] + n[a] * lift for a in range(3)), n, 0.7 * step))
            labels.append((cat, text, _label_point(pts, 0.9 * step)))
            continue
        length = min(0.14 * size, max(0.03 * size, 1.3 * step)) * 0.85
        far = []
        for p, nrm in pts:
            base = tuple(p[a] + nrm[a] * lift for a in range(3))
            if how == 'in':                              # coming in: along the inward normal, the tip on the surface
                d = tuple(-c for c in nrm)
                glyphs.append((cat, True, base, d, length))
                far.append((tuple(base[a] - d[a] * length for a in range(3)), nrm))
            else:                                        # going out: along the outward normal, the tail on the surface
                glyphs.append((cat, False, base, nrm, length))
                far.append((tuple(base[a] + nrm[a] * length for a in range(3)), nrm))
        labels.append((cat, text, _label_point(far, 0.35 * length)))

    # The tint: the part itself, coloured by what each place is -- 0 nothing, else the category of the region it
    # is in (the larger wins: a load is shown over a support)
    out = Shape(lib.libfive_tree_copy(part.ptr))
    out.supports, out.loads, out.notes = supports, drawn_loads, notes
    out._no_handles = True
    out._bounds = (tuple(lo), tuple(hi))
    colour = Shape.wrap(0.0)
    for cat, region, t in pieces:
        colour = colour.max(float(cat) * _inside(region, t))
    out._color_field = colour
    out._color_range = _RANGE
    out._color_label = 'bc:' + ','.join(str(c) for c in sorted(present))
    out._color_map = 'bc'
    out._color_floor = 0.5          # (only the acted-on surfaces are drawn, flat in their colour: not the body they are on, which has its own row)
    # What the viewport draws over it (see the module): (kind, tip at the point, x, y, z, dx, dy, dz, size) and (kind, text, x, y, z)
    out._bc_glyphs = [(int(k), 1 if tip else 0, p[0], p[1], p[2], d[0], d[1], d[2], float(s)) for k, tip, p, d, s in glyphs]
    out._bc_labels = [(int(k), str(t), p[0], p[1], p[2]) for k, t, p in labels]
    if notes:
        print('boundary conditions: ' + '; '.join(notes))
    return out


def _draw_regions(items):
    ''' The conditions of no simulation yet, with no body in the script: the regions they act in, tinted by what they are (there is no body to
        draw the arrows on) '''
    pieces, present = [], set()
    for item in items:
        if isinstance(item, _Support):
            cat = FIXED if all(item.axes) else SLIDING
        elif isinstance(item, _Force):
            cat = FORCE
        else:
            kind = _kind_of(item)
            if kind is None:
                continue
            cat = kind[0]
        pieces.append((cat, item.region))
        present.add(cat)
    if not pieces:
        return Shape.wrap(1e9)
    region = pieces[0][1]
    for _, r in pieces[1:]:
        region = region.min(r)
    # (the regions are where the field is negative: they are the picture; the colour says which is which)
    out = Shape(lib.libfive_tree_copy(region.ptr))
    out._no_handles = True
    boxes = [getattr(r, '_bounds', None) for _, r in pieces]
    if all(boxes):
        out._bounds = (tuple(min(b[0][i] for b in boxes) for i in range(3)), tuple(max(b[1][i] for b in boxes) for i in range(3)))
    colour = Shape.wrap(0.0)
    for cat, r in pieces:
        colour = colour.max(float(cat) * _inside(r, 1e-6))
    out._color_field = colour
    out._color_range = _RANGE
    out._color_label = 'bc:' + ','.join(str(c) for c in sorted(present))
    out._color_map = 'bc'
    return out


def _inside(region, tol):
    ''' 1 where the field is below `tol`, else 0 (a step: compare gives -1, 0 or 1) '''
    return (1.0 - Shape.wrap(region).compare(Shape.wrap(float(tol)))) * 0.5


################################################################################
# Where the symbols go

def _unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v) if n > 0 else (0.0, 0.0, 0.0)


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _project(part, pts, h, rounds=6):
    ''' Points moved onto the part's surface (Newton steps on its field, all at once) and the outward normal at each '''
    pts = [tuple(p) for p in pts]
    normals = [(0.0, 0.0, 1.0)] * len(pts)
    for _ in range(rounds):
        probe = []
        for p in pts:
            probe.append(p)
            for a in range(3):
                for s in (-h, h):
                    q = list(p)
                    q[a] += s
                    probe.append(tuple(q))
        v = evaluate(part, probe)
        moved = []
        for i, p in enumerate(pts):
            f = v[7 * i]
            g = tuple((v[7 * i + 2 + 2 * a] - v[7 * i + 1 + 2 * a]) / (2 * h) for a in range(3))
            g2 = _dot(g, g)
            if g2 <= 1e-12:
                moved.append(p)
                continue
            normals[i] = _unit(g)
            moved.append(tuple(p[a] - f * g[a] / g2 for a in range(3)))
        pts = moved
    return pts, normals


def _project_along(part, pts, d, h, rounds=5):
    ''' Points moved along the unit vector d onto the part's surface (Newton steps for the field along d) and the outward
        normal at each '''
    pts = [tuple(p) for p in pts]
    normals = [d] * len(pts)
    for _ in range(rounds):
        probe = []
        for p in pts:
            probe.append(p)
            for a in range(3):
                for s in (-h, h):
                    q = list(p)
                    q[a] += s
                    probe.append(tuple(q))
        v = evaluate(part, probe)
        moved = []
        for i, p in enumerate(pts):
            f = v[7 * i]
            g = tuple((v[7 * i + 2 + 2 * a] - v[7 * i + 1 + 2 * a]) / (2 * h) for a in range(3))
            gd = _dot(g, d)
            if abs(gd) <= 1e-9:
                moved.append(p)
                continue
            normals[i] = _unit(g)
            moved.append(tuple(p[a] - (f / gd) * d[a] for a in range(3)))
        pts = moved
    return pts, normals


def _tangents(n):
    ''' Two unit vectors across the unit vector n, the same for the same n whatever the part: the world axis that n is
        most across, taken across n, and the cross of n with it '''
    axis = min(range(3), key=lambda a: abs(n[a]))
    e = [0.0, 0.0, 0.0]
    e[axis] = 1.0
    d = _dot(e, n)
    u = _unit(tuple(e[i] - d * n[i] for i in range(3)))
    v = (n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0])
    return u, v


def _hash_cells(points, cell):
    cells = {}
    for k, p in enumerate(points):
        cells.setdefault(tuple(int(math.floor(p[a] / cell)) for a in range(3)), []).append(k)
    return cells


def _near_any(cells, points, cell, q, radius):
    ''' Is a point of `points` within `radius` of q? (`cells` is _hash_cells(points, cell), radius <= cell) '''
    key = tuple(int(math.floor(q[a] / cell)) for a in range(3))
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for k in cells.get((key[0] + dx, key[1] + dy, key[2] + dz), ()):
                    r = points[k]
                    if (r[0] - q[0]) ** 2 + (r[1] - q[1]) ** 2 + (r[2] - q[2]) ** 2 <= radius * radius:
                        return True
    return False


def _bin(n):
    ''' The direction bin of a unit normal: its components rounded to thirds, so that bins are mirror images of each other
        when normals are (the same bins for the same directions, whatever the part) '''
    return tuple(int(math.copysign(math.floor(abs(c) * 3.0 + 0.5), c)) for c in n)


def _within_cap(find, *args, want):
    ''' find(*args, want) -> (points, step), asked again for fewer symbols (a wider step) while it gives more than the cap:
        an array is never cut off, it is made coarser '''
    for _ in range(8):
        pts, step = find(*args, want)
        if len(pts) <= _MAX_SYMBOLS:
            return pts, step
        want = max(1, int(want * _MAX_SYMBOLS / len(pts) * 0.8))
    return pts[:_MAX_SYMBOLS], step


def _surface_samples(part, region, lo, hi, size, tol):
    ''' The part's surface that lies inside `region`, as points on it, one to each half cell, with the outward normal at
        each: (points, normals, cell).  The inside is by the very test the tint uses (`tol`) '''
    # (the points that mark the surface lie just OUTSIDE it: the box reaches a margin beyond the part, or its outermost
    # skin -- the bottom of a lug, the end of a plate -- would never be seen)
    margin = 0.03 * size
    box_lo, box_hi = [lo[i] - margin for i in range(3)], [hi[i] + margin for i in range(3)]
    rb = getattr(region, '_bounds', None)
    if rb:
        for i in range(3):
            box_lo[i] = max(box_lo[i], float(rb[0][i]) - 0.02 * size)
            box_hi[i] = min(box_hi[i], float(rb[1][i]) + 0.02 * size)
    if any(box_hi[i] <= box_lo[i] for i in range(3)):
        return [], [], 0.0
    ext = max(box_hi[i] - box_lo[i] for i in range(3))
    cell = ext / 56.0
    counts = [max(1, int(math.ceil((box_hi[i] - box_lo[i]) / cell))) for i in range(3)]
    pts = []
    for k in range(counts[2]):
        z = box_lo[2] + (k + 0.5) * (box_hi[2] - box_lo[2]) / counts[2]
        for j in range(counts[1]):
            y = box_lo[1] + (j + 0.5) * (box_hi[1] - box_lo[1]) / counts[1]
            for i in range(counts[0]):
                pts.append((box_lo[0] + (i + 0.5) * (box_hi[0] - box_lo[0]) / counts[0], y, z))
    band = max(cell, 0.01 * size)
    r0 = evaluate(region, pts)
    pts = [p for p, rv in zip(pts, r0) if rv < tol + 2.0 * band]          # (the region and its neighbourhood only)
    if not pts:
        return [], [], 0.0
    f = evaluate(part, pts)
    near = [p for p, fv in zip(pts, f) if 0.0 <= fv < band]
    if not near:
        return [], [], 0.0
    onto, normals = _project(part, near, 0.25 * cell, rounds=4)
    f2 = evaluate(part, onto)
    r2 = evaluate(region, onto)
    seen, surf, norm = set(), [], []
    for p, n, fv, rv in zip(onto, normals, f2, r2):
        if abs(fv) > 0.05 * cell or rv >= tol:
            continue
        key = tuple(int(round(p[a] / (0.5 * cell))) for a in range(3))
        if key in seen:
            continue
        seen.add(key)
        surf.append(p)
        norm.append(n)
    return surf, norm, cell


def _force_points(part, region, lo, hi, size, tol, d, want):
    ''' Where the arrows of a force along the unit vector d go: a regular grid across the loaded surface seen along d, an arrow
        wherever the grid meets surface that is turned (roughly) towards the force or away from it -- not the sides,
        which the force only grazes.  The grid is laid out from the middle of what is seen, so that parts that are alike
        get arrays that are alike; where it meets the part more than once along d, only the outermost surface on each side gets an arrow (not the inside of a hole).
        Returns ([(point, outward normal)], step); ([], 0) if there is no such surface '''
    surf, norm, cell = _surface_samples(part, region, lo, hi, size, tol)
    seen = [(p, n) for p, n in zip(surf, norm) if abs(_dot(n, d)) >= 0.5]
    grazed = False
    if not seen:
        # A surface that the force only grazes -- the wall of a bore that is loaded along its axis, a seat -- is loaded all the same: the
        # arrows are shown on it, whatever way it faces (the force is where the region is, not where a face is turned towards it)
        seen = list(zip(surf, norm))
        grazed = bool(seen)
    if not seen:
        return [], 0.0
    u, v = _tangents(d)
    half = 0.5 * cell
    flat = {}
    for p, n in seen:
        flat.setdefault((int(round(_dot(p, u) / half)), int(round(_dot(p, v) / half))), []).append((p, n))
    area = len(flat) * half * half                                      # (what is seen along d)
    step = min(0.16 * size, max(0.035 * size, math.sqrt(area / want)))
    # (the middle of what is seen, to a quarter of a step: a part that is the same on both sides of a plane through the origin
    # gets a grid that is the same on both)
    ac = round(sum(_dot(p, u) for p, _ in seen) / len(seen) / (0.25 * step)) * 0.25 * step
    bc = round(sum(_dot(p, v) for p, _ in seen) / len(seen) / (0.25 * step)) * 0.25 * step
    coords = [(_dot(p, u), _dot(p, v), _dot(p, d), p, n) for p, n in seen]
    cells = {}
    for k, t in enumerate(coords):
        cells.setdefault((int(math.floor(t[0] / step)), int(math.floor(t[1] / step))), []).append(k)
    reach = int(math.ceil(max(max(abs(t[0] - ac), abs(t[1] - bc)) for t in coords) / step)) + 1
    nodes = []
    for i in range(-reach, reach + 1):
        for j in range(-reach, reach + 1):
            ga, gb = ac + i * step, bc + j * step
            # (the surface that the force meets first from each side along d: the outermost of what faces along d, the
            # outermost of what faces against it -- not what lies inside a hole, which the arrows could not reach)
            ahead = behind = None
            for da in (-1, 0, 1):
                for db in (-1, 0, 1):
                    for k in cells.get((int(math.floor(ga / step)) + da, int(math.floor(gb / step)) + db), ()):
                        t = coords[k]
                        if (t[0] - ga) ** 2 + (t[1] - gb) ** 2 > (0.45 * step) ** 2:
                            continue
                        if _dot(t[4], d) > 0.0:
                            if ahead is None or t[2] > ahead[2]:
                                ahead = t
                        elif behind is None or t[2] < behind[2]:
                            behind = t
            for t in (ahead, behind):
                if t is not None:
                    nodes.append(tuple(ga * u[a] + gb * v[a] + t[2] * d[a] for a in range(3)))
    if not nodes:
        return [], 0.0
    # (each arrow on its grid node: the node moved along d onto the surface, where the surface is the part and the region)
    onto, normals = _project_along(part, nodes, d, 0.25 * cell)
    f2 = evaluate(part, onto)
    r2 = evaluate(region, onto)
    out = [(q, n) for q, n, fv, rv in zip(onto, normals, f2, r2)
           if abs(fv) <= 0.05 * cell and rv < tol and (grazed or abs(_dot(n, d)) >= 0.4)]
    if not out:
        return [], 0.0
    if grazed:
        return [(q, n, None) for q, n in out], step     # (an arrow along a wall has no open space in front of it to look for)
    # An arrow stands in the open: the space it points into -- the way the surface faces, out of the part -- is free of the
    # part for a few of its lengths.  Where it is not (the inside of a bore, a slot, the foot of a wall: the part is over it) the arrow
    # is made as short as the free space is, and there is none when less than two probes of it are free.  Each point comes with the
    # free distance (None: all that was looked at)
    reach = 5.0 * step
    along = [d if _dot(n, d) > 0.0 else tuple(-c for c in d) for _, n in out]
    probes, owner, order = [], [], []
    for k, ((q, _), w) in enumerate(zip(out, along)):
        for m in range(1, int(reach / (0.4 * step)) + 1):
            probes.append(tuple(q[a] + w[a] * 0.4 * step * m for a in range(3)))
            owner.append(k)
            order.append(m)
    free = {}
    if probes:
        for k, m, fv in zip(owner, order, evaluate(part, probes)):
            if fv < 0.0 and k not in free:
                free[k] = (m - 1) * 0.4 * step          # (the first probe inside the part: the space before it is free)
    kept = [(q, n, free.get(k)) for k, (q, n) in enumerate(out) if free.get(k, reach) >= 0.8 * step]
    if not kept:
        return [], 0.0
    return kept, step


def _symbol_points(part, region, lo, hi, size, tol, want):
    ''' Where the symbols of a support go: about `want` points over the part's surface inside `region`, and the step
        between them.  Returns ([(point, outward normal)], step); ([], 0) if the region holds no surface.

        The surface is split into FACES: points that are near each other and whose normals fall in the same direction bin
        (a flat face is one face; a curved one is a few strips).  Each face gets a regular array laid out from its own
        middle, along the axes of the face, so that faces that are alike get arrays that are alike.  A point of the array is
        kept only if the surface is there, if it is inside the region by the very test the tint uses, and if the face goes on
        all round it (a symbol is not put on the edge of a face, where the surface has no one direction) '''
    surf, norm, cell = _surface_samples(part, region, lo, hi, size, tol)
    if not surf:
        return [], 0.0
    # faces: points within 1.6 cells of each other whose normals are in the same bin
    link = 1.6 * cell
    cells = _hash_cells(surf, link)
    bins = [_bin(n) for n in norm]
    parent = list(range(len(surf)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for k, p in enumerate(surf):
        key = tuple(int(math.floor(p[a] / link)) for a in range(3))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for m in cells.get((key[0] + dx, key[1] + dy, key[2] + dz), ()):
                        if m > k and bins[m] == bins[k] and \
                                sum((p[a] - surf[m][a]) ** 2 for a in range(3)) <= link * link:
                            parent[find(m)] = find(k)
    groups = {}
    for k in range(len(surf)):
        groups.setdefault(find(k), []).append(k)
    faces = [g for g in groups.values() if len(g) >= 6]                 # (a few points are an edge, not a face)
    if not faces:
        return [], 0.0
    faces.sort(key=lambda g: (round(surf[g[0]][2], 3), round(surf[g[0]][1], 3), round(surf[g[0]][0], 3)))
    info = []
    for g in faces:
        c = tuple(sum(surf[k][a] for k in g) / len(g) for a in range(3))
        n = _unit(tuple(sum(norm[k][a] for k in g) for a in range(3)))
        info.append((g, c, n, len(g) * (0.5 * cell) ** 2))
    total = sum(t[3] for t in info)
    step = min(0.16 * size, max(0.045 * size, math.sqrt(total / want)))
    info = [t for t in info if t[3] >= 0.2 * step * step]             # (a sliver of a face is no place for a symbol)
    out = []
    for g, c, n, area in info:
        face_pts = [surf[k] for k in g]
        fcells = _hash_cells(face_pts, step)
        u, v = _tangents(n)
        reach = int(math.ceil(max(math.sqrt(sum((q[a] - c[a]) ** 2 for a in range(3))) for q in face_pts) / step)) + 1
        cand, index = [], []
        for i in range(-reach, reach + 1):
            for j in range(-reach, reach + 1):
                q = tuple(c[a] + step * (i * u[a] + j * v[a]) for a in range(3))
                if _near_any(fcells, face_pts, step, q, 0.6 * step):
                    cand.append(q)
                    index.append((i, j))
        if not cand:
            continue
        # (the array moved onto the surface; a point stays only where the face goes on all round it)
        qs, qn = _project(part, cand, 0.25 * cell, rounds=4)
        fq = evaluate(part, qs)
        rq = evaluate(region, qs)
        kept = []
        for k, (q, nq) in enumerate(zip(qs, qn)):
            if abs(fq[k]) > 0.05 * cell or rq[k] >= tol or _dot(nq, n) < 0.8:
                continue
            # (the pad is 0.7 steps across: the face goes on past its rim on all four sides, so it never hangs over the edge)
            around = [tuple(q[a] + 0.4 * step * (sx * u[a] + sy * v[a]) for a in range(3))
                      for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
            if not all(_near_any(fcells, face_pts, step, w, 0.25 * step) for w in around):
                continue
            kept.append((q, nq, index[k]))
        out.extend((q, nq) for q, nq, _ in kept)
    if not out and info:
        # (a region too small for the array: one symbol in the middle of its biggest face)
        g, c, n, area = max(info, key=lambda t: t[3])
        qs, qn = _project(part, [c], 0.25 * cell, rounds=4)
        out.append((qs[0], qn[0]))
    if not out:
        return [], 0.0
    return out, step


def _label_point(pts, lift):
    ''' Where the text of a group of symbols goes: above the one nearest to the middle of them (away from the part), `lift` out '''
    c = tuple(sum(p[a] for p, _ in pts) / len(pts) for a in range(3))
    p, n = min(pts, key=lambda pn: sum((pn[0][a] - c[a]) ** 2 for a in range(3)))
    return tuple(p[a] + n[a] * lift for a in range(3))


def _gravity_arrow(g, lo, hi, size):
    ''' The arrow of gravity: over the middle of the part, on the side that the acceleration comes from, pointing down onto it
        the way it acts, ending a little above the part.  Returns (its tail, its direction, its length) or None '''
    d = _unit(g)
    if d == (0.0, 0.0, 0.0):
        return None
    c = tuple(0.5 * (lo[a] + hi[a]) for a in range(3))
    half = 0.5 * sum(abs(d[a]) * (hi[a] - lo[a]) for a in range(3))      # how far the part reaches from its middle along d
    length = 0.18 * size
    tip = tuple(c[a] - d[a] * (half + 0.03 * size) for a in range(3))
    tail = tuple(tip[a] - d[a] * length for a in range(3))
    return tail, d, length
