'''
Boundary conditions you can see.

    from fieldes import *

    part = ...
    conditions = static_boundary_conditions(
        part,
        supports=[fixed(base)],                                   # held here
        loads=[force(lug, (0, -2000, 0)), gravity()])             # pushed here, and its own weight
    conditions                                                    # displayed: supports and loads drawn on the part
    result = static_analysis(part, conditions, material=aluminium, element_size=4)

`static_boundary_conditions(part, supports, loads)` is the problem to solve, without the solving: the supports
and loads of a static analysis, tied to the part they act on.  It is a shape, so it has a row in the model tree
(show, hide, delete) like any other.  It is drawn the way structural analysis programs draw it:

    * the faces that are held or loaded are tinted (blue: fixed support, cyan: sliding support, red: force)
    * a force is an ARRAY of identical arrows over the loaded faces, all along the force, each touching the surface
      with its tip when it pushes in and with its tail when it pulls out, with the total force written beside it
    * a support is an array of flat pads lying on the held faces, with its name beside it
    * gravity is one arrow beside the part along the acceleration, with its value

The arrows, pads and texts are not part of the meshed model: the viewport draws them over it, with a size that follows
the zoom, so they are never cut off by the render region or made ragged by the render's resolution.

`static_analysis`, `modal_analysis` and `topology_optimization` take it: it is the only way to give them
their supports and loads (they take no `supports=` and `loads=` lists).  The tint is the part's own
surface, coloured (and drawn a hair towards the eye, so that it wins over the part if both are shown); a place of the part counts
as in a region when it is within about 1 % of the part's size of it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib.fea import _Support, _Force, _Gravity, _Thermal, _bounds, FeaError
from fieldes.stdlib.fields import evaluate

__all__ = ['static_boundary_conditions', 'StaticBoundaryConditions']

# What a colour of the picture means (the colour map 'bc' of the application: the value is the category)
FIXED, SLIDING, FORCE, GRAVITY = 1, 2, 3, 4
_NAMES = {FIXED: 'Fixed support', SLIDING: 'Sliding support', FORCE: 'Force', GRAVITY: 'Gravity'}
_RANGE = (0.0, 6.0)

# How many symbols a loaded or supported face gets (about), and how big they may be, as shares of the part's size
_FORCE_ARROWS = 48
_SUPPORT_PADS = 24
_MAX_SYMBOLS = 90


class StaticBoundaryConditions(Shape):
    ''' The supports and loads of a static analysis and the part they act on, drawn on the part (see the
        module).  .part, .supports, .loads; pass it to static_analysis(part, conditions, ...) '''

    static_conditions = True          # (what static_analysis & co. look for)
    _no_handles = True                # (not something to drag: FielDes hides the handles button)

    def __repr__(self):
        return 'static_boundary_conditions({})'.format('; '.join(self.describe()) or 'none')

    def describe(self):
        ''' One line for each support and load '''
        out = []
        for s in self.supports:
            axes = ''.join(a for a, on in zip('xyz', s.axes) if on)
            out.append('fixed ({})'.format(axes) if len(axes) == 3 else 'sliding support (fixed in {})'.format(axes or 'nothing'))
        loads = self.loads
        if loads and all(isinstance(c, (list, tuple)) for c in loads):
            loads = [l for c in loads for l in c]
        for l in loads:
            if isinstance(l, _Force):
                n = math.sqrt(sum(c * c for c in l.vector))
                out.append('force {:g} N ({:g}, {:g}, {:g})'.format(n, *l.vector))
            elif isinstance(l, _Gravity):
                out.append('gravity ({:g}, {:g}, {:g}) mm/s2'.format(*l.g))
            elif isinstance(l, _Thermal):
                out.append('thermal expansion (reference {:g})'.format(l.reference))
        return out


def static_boundary_conditions(part, supports=(), loads=()):
    ''' The supports and loads of a static analysis, tied to the `part` they act on and drawn on it.

        supports   fixed(...) items
        loads      force(...), gravity(...) and thermal_expansion(...) items -- for topology_optimization
                   also several load cases, a list of such lists

        Returns a shape to display (the held faces tinted blue, the loaded faces red; arrows for the forces, pads for
        the supports, with their names and values; hide it with the eye of the model tree) that the analyses are given:

            conditions = static_boundary_conditions(part, [fixed(base)], [force(lug, (0, -2000, 0))])
            conditions
            result = static_analysis(part, conditions, material=aluminium) '''
    if not isinstance(part, Shape):
        raise TypeError('static_boundary_conditions: the part must be a Shape')
    supports = list(supports if isinstance(supports, (list, tuple)) else [supports])
    loads = list(loads if isinstance(loads, (list, tuple)) else [loads])
    for s in supports:
        if not isinstance(s, _Support):
            raise TypeError('supports must be fixed(...) items')
    cases = loads and all(isinstance(c, (list, tuple)) for c in loads)
    drawn_loads = [l for c in loads for l in c] if cases else loads        # (load cases are all drawn)
    for l in drawn_loads:
        if not isinstance(l, (_Force, _Gravity, _Thermal)):
            raise TypeError('loads must be force(...), gravity(...) or thermal_expansion(...) items')

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
        pts, step = _within_cap(_symbol_points, part, s.region, lo, hi, size, tol, want=_SUPPORT_PADS)
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
            pts, step = _within_cap(_force_points, part, l.region, lo, hi, size, tol, d, want=_FORCE_ARROWS)
            if not pts:
                notes.append('a force acts where the part is not')
                continue
            length = min(0.14 * size, max(0.03 * size, 1.3 * step)) * (0.75 + 0.25 * n / biggest)
            far = []                                     # the end of each arrow that is away from the surface
            for p, nrm in pts:
                dn = _dot(d, nrm)
                base = tuple(p[a] + nrm[a] * lift for a in range(3))
                if dn < 0.0:                             # pushing in: the tip touches the surface
                    glyphs.append((FORCE, True, base, d, length))
                    far.append((tuple(base[a] - d[a] * length for a in range(3)), nrm))
                else:                                    # pulling out: the tail is on the surface
                    glyphs.append((FORCE, False, base, d, length))
                    far.append((tuple(base[a] + d[a] * length for a in range(3)), nrm))
            labels.append((FORCE, '{:g} N'.format(float('{:.4g}'.format(n))), _label_point(far, 0.35 * length)))
        elif isinstance(l, _Gravity):
            arrow = _gravity_arrow(l.g, lo, hi, size)
            if arrow is not None:
                p, d, length = arrow
                glyphs.append((GRAVITY, False, p, d, length))
                g = math.sqrt(sum(c * c for c in l.g))
                labels.append((GRAVITY, 'Gravity {:.4g} m/s\u00b2'.format(g / 1000.0),
                               tuple(p[i] - d[i] * 0.25 * length for i in range(3))))
                present.add(GRAVITY)

    # The tint: the part itself, coloured by what each place is -- 0 nothing, else the category of the region it
    # is in (the larger wins: a load is shown over a support)
    out = StaticBoundaryConditions(lib.libfive_tree_copy(part.ptr))
    out.part, out.supports, out.loads, out.notes = part, supports, loads, notes
    out._bounds = (tuple(lo), tuple(hi))
    colour = Shape.wrap(0.0)
    for cat, region, t in pieces:
        colour = colour.max(float(cat) * _inside(region, t))
    out._color_field = colour
    out._color_range = _RANGE
    out._color_label = 'bc:' + ','.join(str(c) for c in sorted(present))
    out._color_map = 'bc'
    # What the viewport draws over it (see the module): (kind, tip at the point, x, y, z, dx, dy, dz, size) and (kind, text, x, y, z)
    out._bc_glyphs = [(int(k), 1 if tip else 0, p[0], p[1], p[2], d[0], d[1], d[2], float(s)) for k, tip, p, d, s in glyphs]
    out._bc_labels = [(int(k), str(t), p[0], p[1], p[2]) for k, t, p in labels]
    if notes:
        print('static_boundary_conditions: ' + '; '.join(notes))
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
           if abs(fv) <= 0.05 * cell and rv < tol and abs(_dot(n, d)) >= 0.4]
    if not out:
        return [], 0.0
    # An arrow stands in the open: the space it points into -- the way the surface faces, out of the part -- is free of the
    # part for a few of its lengths.  What is not (the bottom of a bore, a slot, the foot of a wall: the part is over it) gets no arrow
    reach = 5.0 * step
    along = [d if _dot(n, d) > 0.0 else tuple(-c for c in d) for _, n in out]
    probes, owner = [], []
    for k, ((q, _), w) in enumerate(zip(out, along)):
        for m in range(1, int(reach / (0.4 * step)) + 1):
            probes.append(tuple(q[a] + w[a] * 0.4 * step * m for a in range(3)))
            owner.append(k)
    blocked = set()
    if probes:
        for k, fv in zip(owner, evaluate(part, probes)):
            if fv < 0.0:
                blocked.add(k)
    out = [t for k, t in enumerate(out) if k not in blocked]
    if not out:
        return [], 0.0
    return out, step


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
            around = [tuple(q[a] + 0.3 * step * (sx * u[a] + sy * v[a]) for a in range(3))
                      for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
            if not all(_near_any(fcells, face_pts, step, w, 0.4 * step) for w in around):
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
