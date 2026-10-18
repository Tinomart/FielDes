# mfgtest.py: the checks that the manufacturing tests share.  Nothing here uses a number that the optimizer printed about itself:
#
#   compare()      two fields (the part and what the optimizer made of it) read at the same many points of space, to find where material was added and where it was cut, and how much
#   debris()       material added or cut farther from the part than any remedy can reach: a fragment that has nothing to do with the part
#   run_length()   the length of the material (or the air) along a line, from a start point: the thickness of a wall, the width of a gap, as a tape measure gives it
#   Table          one line for each check, PASS or FAIL, what was found and what was expected
#
# Table is one line for each check.  The files read back are the ones of a print (3MF, DXF, SVG); no test exports a mesh to look at it.
# All of it is test code: it reads fields at points and files from disk.  No product code uses it.
import math
import os
import random
import time

from fieldes import *


class Table:
    ''' One line per check: PASS or FAIL, what was found, what was expected. '''

    def __init__(self, title):
        self.title = title
        self.rows = []
        self.t0 = time.time()
        print('=' * 100)
        print(title)
        print('=' * 100)

    def check(self, name, ok, got='', expected=''):
        ok = bool(ok)
        got, expected = str(got), str(expected)
        self.rows.append((ok, name, got, expected))
        line = ('PASS  ' if ok else 'FAIL  ') + name
        if got != '' or expected != '':
            line += '   | found: %s' % got
            if expected != '':
                line += '   | expected: %s' % expected
        print(line)
        return ok

    def near(self, name, got, expected, tol, unit=''):
        ''' |got - expected| <= tol '''
        return self.check(name, abs(got - expected) <= tol, '%.4g%s' % (got, unit), '%.4g%s +- %.3g' % (expected, unit, tol))

    def between(self, name, got, low, high, unit=''):
        return self.check(name, low <= got <= high, '%.4g%s' % (got, unit), '%.4g .. %.4g%s' % (low, high, unit))

    def failed(self):
        return [r for r in self.rows if not r[0]]

    def summary(self):
        bad = self.failed()
        print('-' * 100)
        if bad:
            print('%s: %d of %d checks FAILED (%.0f s)' % (self.title, len(bad), len(self.rows), time.time() - self.t0))
            for _, name, got, expected in bad:
                print('   FAILED: %s   | found: %s | expected: %s' % (name, got, expected))
        else:
            print('%s: all %d checks passed (%.0f s)' % (self.title, len(self.rows), time.time() - self.t0))
        return not bad


def points_in(lo, hi, n, seed=1):
    ''' n random points of the box, the same for the same seed '''
    rng = random.Random(seed)
    return [(rng.uniform(lo[0], hi[0]), rng.uniform(lo[1], hi[1]), rng.uniform(lo[2], hi[2])) for _ in range(n)]


def compare(part, result, lo, hi, n=200000, seed=3):
    ''' The part and the result read at n points of the box (lo, hi): where material was added (the part's field above 0, the result's at or below), and where it was cut.
        Returns a dict: added / removed = [(point, the part's own field there)], vol_added, vol_removed, vol_part (mm^3, by counting points), box (mm^3) '''
    pts = points_in(lo, hi, n, seed)
    a = evaluate(part, pts)
    b = evaluate(result, pts)
    box = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])
    added, removed, inside = [], [], 0
    for p, x, y in zip(pts, a, b):
        if x <= 0:
            inside += 1
            if y > 0:
                removed.append((p, x))
        elif y <= 0 and x > 1e-3:           # (the result is grown by a hair of a few microns everywhere: a point that close to the part's surface is not material that was added)
            added.append((p, x))
    return {'added': added, 'removed': removed, 'pts': pts, 'a': a, 'b': b, 'n': n, 'box': box,
            'vol_added': box * len(added) / n, 'vol_removed': box * len(removed) / n, 'vol_part': box * inside / n}


def changed_volumes(part, result, lo, hi, n_coarse=150000, n_dense=150000, cell=1.5, seed=5):
    ''' The volumes added and cut, measured in two steps: a coarse look at the whole box finds where the result differs from the part; those places (the cells of `cell` mm that
        hold a difference, and their neighbours) are then sampled densely, so that a layer a tenth of a millimetre thick has points in it.  Returns (added, removed) in mm^3 and the
        number of cells that were looked at.  (Written for the tests: it shares no code with the optimizer's own measurement.) '''
    coarse = compare(part, result, lo, hi, n_coarse, seed)
    cells = set()
    for p, _ in coarse['added'] + coarse['removed']:
        c = (int(math.floor((p[0] - lo[0]) / cell)), int(math.floor((p[1] - lo[1]) / cell)), int(math.floor((p[2] - lo[2]) / cell)))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    cells.add((c[0] + dx, c[1] + dy, c[2] + dz))
    if not cells:
        return 0.0, 0.0, 0
    cells = sorted(cells)
    rng = random.Random(seed + 1)
    pts = []
    for _ in range(n_dense):
        c = cells[rng.randrange(len(cells))]
        pts.append((lo[0] + (c[0] + rng.random()) * cell, lo[1] + (c[1] + rng.random()) * cell, lo[2] + (c[2] + rng.random()) * cell))
    a = evaluate(part, pts)
    b = evaluate(result, pts)
    # (the result is grown by a hair of a few microns everywhere, as `compare` says: a point that close to the part's surface is not material that was added.  Counted, each of those points weighs
    # a cell's volume over the number of points -- 1 mm^3 and more -- and a few of them made 4.87 mm^3 of "added" where the part was left alone: d5)
    # (the cells next to the ones that hold a difference reach past the box -- by up to a cell, 1.5 mm --, and what lies out there is not what is measured: a box made to leave out the draft on the end face
    # of a wall, which is a real addition, counted it all the same -- 8000 points of 150000 in d5.  The points are uniform over the cells, so counting only those inside the box stays unbiased)
    inbox = [all(lo[i] <= p[i] <= hi[i] for i in range(3)) for p in pts]
    added = sum(1 for x, y, ib in zip(a, b, inbox) if ib and x > 1e-3 and y <= 0)
    removed = sum(1 for x, y, ib in zip(a, b, inbox) if ib and x <= 0 < y)
    cell_volume = len(cells) * cell ** 3
    return cell_volume * added / n_dense, cell_volume * removed / n_dense, len(cells)


def debris(cmp, reach):
    ''' How many of the points where material was added lie farther than `reach` from the part?  The part's own field is a lower bound of the distance to it outside
        (a hit is certain).  Returns the points. '''
    return [p for p, x in cmp['added'] if x > reach]


def floating_clusters(cmp, cell=1.5, touch=0.6):
    ''' Pieces of added material that do not touch the part: the added sample points are joined into clusters (points in the same or a neighbouring cell of `cell` mm), and a
        cluster none of whose points is within `touch` mm of the part (by the part's own field) is a fragment floating beside it.  Returns the clusters (lists of points). '''
    cells = {}
    for p, a in cmp['added']:
        c = (int(math.floor(p[0] / cell)), int(math.floor(p[1] / cell)), int(math.floor(p[2] / cell)))
        cells.setdefault(c, []).append((p, a))
    seen, out = set(), []
    for start in cells:
        if start in seen:
            continue
        stack, members = [start], []
        seen.add(start)
        while stack:
            c = stack.pop()
            members.extend(cells[c])
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        n = (c[0] + dx, c[1] + dy, c[2] + dz)
                        if n in cells and n not in seen:
                            seen.add(n)
                            stack.append(n)
        if min(a for _, a in members) > touch:
            out.append([p for p, _ in members])
    return out


def changed_far(cmp, far):
    ''' Points where the result differs from the part, and that are farther than `far` from its surface: the part's field is more than `far` in size there (inside or out).
        Returns the points (outside: added; inside: cut). '''
    out = []
    for p, x, y in zip(cmp['pts'], cmp['a'], cmp['b']):
        if abs(x) > far and ((x > 0) != (y > 0)):
            out.append(p)
    return out


def box_of(points):
    ''' (lo, hi) of a list of points, or None '''
    if not points:
        return None
    return (tuple(min(p[i] for p in points) for i in range(3)), tuple(max(p[i] for p in points) for i in range(3)))


def run_length(field, start, direction, length=60.0, n=6000, inside=True):
    ''' A tape measure through a field: from `start`, along `direction` both ways, how long is the unbroken run of material (inside=True: the field at or below 0) or of air
        (inside=False) that holds `start`?  Returns (behind, ahead) in mm: the distances to where the run ends.  `start` must be in the run. '''
    d = math.sqrt(sum(c * c for c in direction))
    u = tuple(c / d for c in direction)
    ts = [-length + 2.0 * length * i / n for i in range(n + 1)]
    pts = [(start[0] + u[0] * t, start[1] + u[1] * t, start[2] + u[2] * t) for t in ts]
    vals = evaluate(field, pts)
    ok = [(v <= 0) == inside for v in vals]
    mid = min(range(len(ts)), key=lambda i: abs(ts[i]))
    if not ok[mid]:
        return (0.0, 0.0)
    i = mid
    while i > 0 and ok[i - 1]:
        i -= 1
    j = mid
    while j < n and ok[j + 1]:
        j += 1
    return (-ts[i], ts[j])


def read_3mf(path):
    ''' The objects of a 3MF file: [{'name', 'type', 'triangles', 'vertices', 'volume'}] (volume from the triangles, mm^3) '''
    import zipfile
    import xml.etree.ElementTree as ET
    with zipfile.ZipFile(path) as z:
        model = [n for n in z.namelist() if n.lower().endswith('.model')][0]
        root = ET.fromstring(z.read(model))
    out = []
    for obj in root.iter():
        if not obj.tag.endswith('}object') and obj.tag != 'object':
            continue
        verts, tris = [], []
        for e in obj.iter():
            if e.tag.endswith('vertex'):
                verts.append((float(e.get('x')), float(e.get('y')), float(e.get('z'))))
            elif e.tag.endswith('triangle'):
                tris.append((int(e.get('v1')), int(e.get('v2')), int(e.get('v3'))))
        vol = 0.0
        for a, b, c in tris:
            pa, pb, pc = verts[a], verts[b], verts[c]
            vol += (pa[0] * (pb[1] * pc[2] - pb[2] * pc[1]) - pa[1] * (pb[0] * pc[2] - pb[2] * pc[0]) + pa[2] * (pb[0] * pc[1] - pb[1] * pc[0])) / 6.0
        out.append({'name': obj.get('name'), 'type': obj.get('type'), 'triangles': len(tris), 'vertices': len(verts), 'volume': vol})
    return out


def read_dxf_layers(path):
    ''' (the names of the DXF layers that hold entities, the number of entities) '''
    with open(path, encoding='utf-8', errors='replace') as f:
        lines = [l.strip() for l in f.read().splitlines()]
    layers, entities = set(), 0
    for i in range(0, len(lines) - 1, 2):
        code, value = lines[i], lines[i + 1]
        if code == '0' and value in ('POLYLINE', 'LWPOLYLINE', 'LINE', '3DFACE'):
            entities += 1
        if code == '8':
            layers.add(value)
    return layers, entities


def svg_area(path):
    ''' The area (mm^2) of the filled paths of an SVG written by export_slice: each 'M ... Z' contour by the shoelace formula, the even-odd fill: contours that lie inside another are holes '''
    import re
    text = open(path, encoding='utf-8').read()
    d = re.search(r' d="([^"]*)"', text).group(1)
    polys = []
    for part in d.split('M')[1:]:
        nums = [float(v) for v in re.findall(r'-?\d+\.?\d*(?:e-?\d+)?', part)]
        polys.append([(nums[i], nums[i + 1]) for i in range(0, len(nums) - 1, 2)])
    areas = []
    for p in polys:
        a = 0.0
        for i in range(len(p)):
            x1, y1 = p[i]
            x2, y2 = p[(i + 1) % len(p)]
            a += x1 * y2 - x2 * y1
        areas.append(abs(a) / 2.0)
    return sum(areas), len(polys)


def scratch(name='manufacturing_tests'):
    ''' A folder in the system's temporary folder for the files a test writes (nothing is written into the project) '''
    import tempfile
    d = os.path.join(tempfile.gettempdir(), 'fieldes_' + name)
    os.makedirs(d, exist_ok=True)
    return d
