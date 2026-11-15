'''
A Cartesian grid fitted to the body (H193: the user, 2026-11-13, "For orientation what a good and thorough meshing suite is use: Salome ... The task is to do it CORRECTLY"): the method of Salome's
"Body Fitting" (StdMeshers_Cartesian_3D, read in its source), made on the body's field.

  1. the grid     cubes of the element size over the box of the body (one cube more all round), a whole number along each axis
  2. the crossings every link of the grid -- every edge of a cube -- that runs from a node inside the body to one outside it is crossed by the surface: the crossing is found on the field (halving, 28 times:
                  the node lies on the surface to a hundred millionth of a cube).  A node of the grid within a tenth of a cube of the surface, inside it or outside, is ON it (SNAP), and the crossing of a link that
                  ends in it is that node: no cell has an edge shorter than a tenth of a cube
  3. the cells   a cube with all its corners inside is a hexahedron.  A cube the surface cuts is the polyhedron of what is inside it: the inside part of each of its six faces (a polygon of the corners that are
                  inside and the crossings of its sides -- when two opposite corners are inside the field at the middle of the face says whether they are joined) and the polygon of the surface between the
                  crossings (the loops that the cuts across the faces make).  The cell is named by what it is made of: 8 nodes and six quadrilaterals a hexahedron, 4 nodes a tetrahedron, 6 nodes with two
                  triangles a prism, 5 nodes with a quadrilateral a pyramid, and anything else a polyhedron
  4. the threshold a cut cell that holds no more than 1 / threshold of a cube (a tenth by default; Salome's "Size Threshold" has a quarter) is JOINED to the cell it shares the most face with (the cell that Salome leaves out
                  opens the surface there: the faces of its neighbours become the boundary, a staircase up to a third of a cube off it, in faces that lean up to 45 degrees from it); the faces the two cells shared are
                  inside the joined cell, the polygon of the surface that the small cell had stays the boundary, and no cell holds less than the threshold.  threshold=1e9 keeps every cut cell as it is
  4b. the nodes  a node of the grid that the snap took for ON the surface (a tenth of a cube from it, inside or outside) is put on it (_settle): the nodes of the boundary are exactly on the surface, and the cells that
                  hold the node follow (a cube with a corner moved by a tenth of its side keeps a scaled Jacobian of about 0.8)

  5. quanta      (element='hex', Salome's Quanta) no cut cell is made at all: a cube that the surface cuts is a whole cube of the grid when what is inside it holds at least `quanta` of it, else left out

The nodes of the boundary are ON the surface (the nodes the snap took for on it are put there, step 4b), the inside is a grid of cubes.  A sharp edge that runs through the cut cells is followed (Salome's "Implement Edges", on by default here): where the cut
across a face of the grid joins two crossings whose surface normals differ by more than `edge_angle`, a node is put on the edge -- where the tangents of the two sides, in the plane of the face, meet --
and the polygon of the surface in the cell is cut in two along the line between the two such nodes of the cell.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import math
from array import array

# The corners of a hexahedron as the lattice offsets of the cell, its edges and its faces (the corners in the order that has the normal out)
CORNER = ((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1))
EDGE = ((0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7))
FACE = ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))

CROSS_STEPS = 28
SNAP = 0.1                                           # (a tenth of a cube: see Fit.run.  It was a fiftieth: a surface that crosses a link at 0.02 to 0.05 of a cube from a node, just outside that, left a cell with an edge as short and a
                                                     # skewness of 0.98 -- on the STEP bracket at 6 mm 64 of 1,880 cells were over 0.85, 12 at a tenth of a cube and 8 at 0.12 -- and the surface is no farther from the nodes of the boundary than the
                                                     # cut cells that the threshold leaves out make it)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class Fit(object):
    ''' The grid, the field at its nodes, the crossings, and the cells '''

    def __init__(self, body, lo, hi, size, threshold=10.0, edges=True, edge_angle=40.0, quanta=None, merge=True):
        from fieldes.stdlib import meshing as M
        self.M = M
        self.body = body
        self.size = float(size)
        # (the field is evaluated in single precision: on a face of the body far from the origin a point reads a few millionths of a millimetre instead of 0 -- +4.3e-6 at 70 mm -- and a surface that lies ON a face of the grid
        # (every face of a box) then has no sign change along the links in it: the crossing search found none, the cube lost its face on the surface and was half a cube, or was left out: an L of two boxes 70 mm from the origin
        # had 18 cubes of 75 % less than at the origin.  A value within `noise` of 0 is 0 for the crossings and the middles of faces)
        self.noise = 5e-7 * (max(abs(float(c)) for c in list(lo) + list(hi)) + self.size)
        # Salome's "Quanta" (StdMeshers_Cartesian_3D_Hexahedron::addVolumes, read in its source): when it is set no cut cell is made -- a cube that the surface cuts becomes a whole hexahedron of the grid,
        # its eight nodes at the corners of the cube (the outside ones too: "add all nodes outside the body. Used to reconstruct the hexahedrals when polys are not desired!", StdMeshers_Cartesian_3D_Grid.cxx),
        # when what is inside it holds at least `quanta` of the cube (polyVol / hexaVolume >= quanta), and is left out when it holds less.  The boundary of the mesh is then a staircase of whole cubes
        self.quanta = None if quanta is None else float(quanta)
        if self.quanta is not None:
            edges = False
        n = [max(2, int(round((float(hi[a]) - float(lo[a])) / size))) for a in range(3)]
        self.d = [(float(hi[a]) - float(lo[a])) / n[a] for a in range(3)]
        self.n = [n[a] + 2 for a in range(3)]                  # (one cube more all round: the surface may lie on the box)
        self.o = [float(lo[a]) - self.d[a] for a in range(3)]
        self.threshold = float(threshold)
        self.merge = bool(merge)                                # (a cut cell that is small is joined to a neighbour; False leaves it out, as Salome does)
        self.edges = bool(edges)
        self.cos_edge = math.cos(math.radians(float(edge_angle)))
        self.pos = []
        self.cross = {}
        self.centre = {}
        self.normal = {}
        self.gradient_length = {}
        self.edge_node = {}                                   # (a node on an edge of the body that a cut across a face of the grid was bent at)
        self.edge_ids = set()
        self.edge_axis = {}                                   # (the axis the face of the grid that holds an edge node is perpendicular to)

    def gid(self, i, j, k):
        return (i * (self.n[1] + 1) + j) * (self.n[2] + 1) + k

    def run(self):
        ''' The cells: [(kind, nodes)], kind 'hex8' 'tet4' 'prism6' 'pyra5' (the nodes in the order of meshing.py's tables) or 'poly' (the nodes are the faces: a list of lists, each turned outward) '''
        M = self.M
        nx, ny, nz = self.n
        ox, oy, oz = self.o
        dx, dy, dz = self.d
        pts = [(ox + i * dx, oy + j * dy, oz + k * dz) for i in range(nx + 1) for j in range(ny + 1) for k in range(nz + 1)]
        self.pos = list(pts)
        values = M._eval(self.body, pts)
        eps = SNAP * self.size                          # (a node within this of the surface is on it: the surface moves by at most that to the node, and no cell has an edge shorter than it)
        inside = [v <= eps for v in values]
        self.inside = inside
        cube = dx * dy * dz
        # the cells that have a corner inside; the links of the ones that are cut
        cells = []
        links = {}
        for i in range(nx):
            for j in range(ny):
                for k in range(nz):
                    ids = [self.gid(i + a, j + b, k + c) for (a, b, c) in CORNER]
                    s = 0
                    for g in ids:
                        if inside[g]:
                            s += 1
                    if s == 0:
                        continue
                    cells.append((ids, s))
                    if s == 8:
                        continue
                    for a, b in EDGE:
                        ga, gb = ids[a], ids[b]
                        if inside[ga] != inside[gb]:
                            links[(ga, gb) if ga < gb else (gb, ga)] = (ga, gb) if inside[ga] else (gb, ga)
        # the crossings, all links together
        keys = list(links)
        pin = [self.pos[links[key][0]] for key in keys]
        pout = [self.pos[links[key][1]] for key in keys]
        for _ in range(CROSS_STEPS):
            mid = [((p[0] + q[0]) * 0.5, (p[1] + q[1]) * 0.5, (p[2] + q[2]) * 0.5) for p, q in zip(pin, pout)]
            v = M._eval(self.body, mid)
            for m in range(len(keys)):
                if v[m] <= self.noise:
                    pin[m] = mid[m]
                else:
                    pout[m] = mid[m]
        tiny = eps
        for m, key in enumerate(keys):
            node = links[key][0]
            q = self.pos[node]
            if abs(pin[m][0] - q[0]) < tiny and abs(pin[m][1] - q[1]) < tiny and abs(pin[m][2] - q[2]) < tiny:
                self.cross[key] = node                       # (the surface is at the node: the crossing is that node)
            else:
                self.pos.append(pin[m])
                self.cross[key] = len(self.pos) - 1
        # the faces whose inside corners are diagonal: the field at their middle says which way the inside goes
        amb = {}
        for ids, s in cells:
            if s == 8:
                continue
            for quad in FACE:
                g = [ids[c] for c in quad]
                if inside[g[0]] == inside[g[2]] and inside[g[1]] == inside[g[3]] and inside[g[0]] != inside[g[1]]:
                    key = tuple(sorted(g))
                    if key not in amb:
                        amb[key] = tuple(sum(self.pos[x][c] for x in g) / 4.0 for c in range(3))
        ak = list(amb)
        for key, v in zip(ak, M._eval(self.body, [amb[x] for x in ak]) if ak else ()):
            self.centre[key] = v <= self.noise
        if self.edges:
            self._normals()
        out = []
        self.dropped = 0
        self.merged = 0                                       # (the small cut cells that were joined to the cell they share their largest face with)
        self.flat = 0                                         # (the pieces of a cut cube that have no thickness: the surface lies on a face of the cube)
        self.settled = 0                                      # (the nodes of the grid that were put on the surface)
        self.whole = 0                                        # (the cubes that lie wholly in the body)
        hexes = []
        pieces = []                                           # (the faces and the volume of every piece the surface leaves in a cut cube, when cut cells are made)
        for ids, s in cells:
            if s == 8:
                if self.quanta is None and self.merge:
                    hexes.append(ids)
                else:
                    out.append(('hex8', ids))
                self.whole += 1
                continue
            if self.quanta is not None:
                # (a whole cube of the grid when what the surface leaves inside it, all its pieces, is at least `quanta` of it)
                inner = 0.0
                for faces in self._cut_cell(ids):
                    inner += self.volume(faces)
                if inner >= self.quanta * cube:
                    out.append(('hex8', ids))
                elif inner > 1e-9 * cube:
                    self.dropped += 1                         # (a cube that only touches the surface -- the ring round a box -- is not a cut cube)
                continue
            for faces in self._cut_cell(ids):
                volume = self.volume(faces)
                if not volume > 1e-9 * cube:
                    self.flat += 1                            # (a flat piece: the surface lies on a face of the cube and leaves no thickness)
                    continue
                if self.merge:
                    pieces.append((faces, volume))
                elif volume > cube / self.threshold:
                    out.append(self._named(faces))
                else:
                    self.dropped += 1
        if self.quanta is None and self.merge:
            self._settle(values, eps, hexes, pieces)
            self._settle_edges(pieces)
            out.extend(self._join(hexes, pieces, cube))
        self.cells = out
        return out

    def _area(self, face):
        """ The area of a face (the nodes in order): half the length of the sum of the cross products of the fan """
        pos = self.pos
        p0 = pos[face[0]]
        sx = sy = sz = 0.0
        for m in range(1, len(face) - 1):
            p1, p2 = pos[face[m]], pos[face[m + 1]]
            ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
            vx, vy, vz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
            sx += uy * vz - uz * vy
            sy += uz * vx - ux * vz
            sz += ux * vy - uy * vx
        return 0.5 * math.sqrt(sx * sx + sy * sy + sz * sz)

    def _settle(self, values, eps, hexes, pieces):
        """ The nodes of the grid that the snap took for ON the surface (within a tenth of a cube of it, inside or outside) are put ON it: each goes along the direction of the field by its value, three steps, never farther
            than the snap (a tenth of a cube) from where it was.  The cells round it follow (a cube with a corner moved by a tenth of its side is a hexahedron whose scaled Jacobian is still about 0.8); a node whose
            move leaves a whole cube with a scaled Jacobian under 0.3 stays where it was.  Without this the nodes of the skin stood up to a tenth of a cube off the surface (0.37 mm of 4 mm on the bracket) while the
            nodes of the tetrahedra are within a fiftieth of an element of it.  It is done before the small and the wedge-shaped cut cells are joined, which then see the cells as they are """
        grid_nodes = (self.n[0] + 1) * (self.n[1] + 1) * (self.n[2] + 1)
        used = set()
        for ids in hexes:
            used.update(ids)
        for faces, _ in pieces:
            for fc in faces:
                used.update(fc)
        todo = [g for g in used if g < grid_nodes and self.noise < abs(values[g]) <= eps]
        if not todo:
            return
        h = 0.01 * self.size
        start = {g: self.pos[g] for g in todo}
        cur = dict(start)
        for _ in range(3):
            pts = []
            for g in todo:
                p = cur[g]
                pts.append(p)
                for axis in range(3):
                    for sign in (-h, h):
                        q = list(p)
                        q[axis] += sign
                        pts.append(tuple(q))
            vals = self.M._eval(self.body, pts)
            for m, g in enumerate(todo):
                v = vals[7 * m:7 * m + 7]
                gx, gy, gz = [(v[2 + 2 * c] - v[1 + 2 * c]) / (2.0 * h) for c in range(3)]
                ln2 = gx * gx + gy * gy + gz * gz
                if ln2 < 1e-12:
                    continue
                p = cur[g]
                step = v[0] / ln2
                q = (p[0] - step * gx, p[1] - step * gy, p[2] - step * gz)
                if math.dist(q, start[g]) <= eps * 1.5:
                    cur[g] = q
        moved = set(g for g in todo if cur[g] != start[g])
        flat = array('d')
        for p in self.pos:
            flat.extend(p)
        for g in moved:
            flat[3 * g:3 * g + 3] = array('d', cur[g])
        incident = {}
        for hid, ids in enumerate(hexes):
            for g in ids:
                if g in moved:
                    incident.setdefault(g, []).append(hid)
        for _ in range(3):
            bad = set()
            seen = set()
            for g in moved:
                for hid in incident.get(g, ()):
                    if hid in seen:
                        continue
                    seen.add(hid)
                    if self.M._cell_jacobian(flat, 'hex8', hexes[hid]) < 0.3:
                        bad.update(x for x in hexes[hid] if x in moved)
            if not bad:
                break
            for x in bad:                                     # (a node whose move leaves a whole cube bent stays where it was)
                flat[3 * x:3 * x + 3] = array('d', start[x])
                moved.discard(x)
        for g in moved:
            self.pos[g] = cur[g]
        self.settled = len(moved)

    def _settle_edges(self, pieces):
        """ The nodes on an edge of the body (made by _edge_node in the plane of a face of the grid, where the tangent lines of the two sides of the cut meet) are put ON the surface: each goes along the field's direction
            in that plane (the grid face stays flat) by its value, three steps, never more than a tenth of a cube.  The tangent lines meet a little off a curved edge (0.03 of a cube on a tilted cylinder) """
        used = set()
        for faces, _ in pieces:
            for fc in faces:
                used.update(fc)
        todo = [g for g in used if g in self.edge_axis]
        if not todo:
            return
        h = 0.01 * self.size
        limit = 0.1 * self.size
        start = {g: self.pos[g] for g in todo}
        cur = dict(start)
        for _ in range(3):
            pts = []
            for g in todo:
                p = cur[g]
                pts.append(p)
                for axis in range(3):
                    for sign in (-h, h):
                        q = list(p)
                        q[axis] += sign
                        pts.append(tuple(q))
            vals = self.M._eval(self.body, pts)
            for m, g in enumerate(todo):
                v = vals[7 * m:7 * m + 7]
                grad = [(v[2 + 2 * c] - v[1 + 2 * c]) / (2.0 * h) for c in range(3)]
                grad[self.edge_axis[g]] = 0.0
                ln2 = grad[0] * grad[0] + grad[1] * grad[1] + grad[2] * grad[2]
                if ln2 < 1e-12:
                    continue
                p = cur[g]
                step = v[0] / ln2
                q = (p[0] - step * grad[0], p[1] - step * grad[1], p[2] - step * grad[2])
                if math.dist(q, start[g]) <= limit:
                    cur[g] = q
        for g in todo:
            self.pos[g] = cur[g]

    def _join(self, hexes, pieces, cube):
        """ The cells when the small cut cells are JOINED to a neighbour instead of being left out.  A cut cell that holds no more than 1 / threshold of a cube is made one cell with the cell it shares its largest face
            with (a whole cube, or another cut cell: the one with the larger volume; again, while the joined cell is still that small): the faces they share are inside the new cell and go, the others stay, so the
            polygon of the surface that the small cell had stays on the boundary and the surface is not opened up where the small cell was (Salome's threshold leaves such a cell out and the faces of its neighbours
            become the boundary: a staircase that lies up to a third of a cube off the surface, in faces that lean up to 45 degrees from it).  A cut cell with no neighbour at all is left out.  Returns the (kind, nodes) of all the cells """
        limit = cube / self.threshold
        count = len(hexes)
        faces_of = []
        volume = []
        for ids in hexes:
            faces_of.append([tuple(ids[c] for c in quad) for quad in FACE])
            volume.append(cube)
        for faces, v in pieces:
            faces_of.append([tuple(f) for f in faces])
            volume.append(self.volume(faces))                 # (again: the nodes that were put on the surface have moved)
        # (a face of no area: three nodes on one straight line, which is what a triangle of the surface becomes when a node of the grid is put on the straight crease between two crossings.  It goes, and its middle
        # node is added to the faces that have its long edge, in this cell and in the one next to it, so that the cells stay closed and no face has no area)
        pos = self.pos
        changed = set()
        for _ in range(4):
            wanted = {}
            for i, fl in enumerate(faces_of):
                for fi, f in enumerate(fl):
                    if len(f) == 3:
                        lens = [(math.dist(pos[f[k]], pos[f[(k + 1) % 3]]), k) for k in range(3)]
                        longest, k = max(lens)
                        if longest > 0.0 and self._area(f) < 1e-3 * longest * longest:
                            p, q = f[k], f[(k + 1) % 3]
                            wanted[(p, q) if p < q else (q, p)] = (f[(k + 2) % 3], i, fi)
            if not wanted:
                break
            users = {}
            for i, fl in enumerate(faces_of):
                for fi, f in enumerate(fl):
                    m = len(f)
                    for k in range(m):
                        a, b = f[k], f[(k + 1) % m]
                        key = (a, b) if a < b else (b, a)
                        if key in wanted:
                            users.setdefault(key, []).append((i, fi, k))
            inserts = {}
            kill = set()
            for key, (mid, i0, fi0) in wanted.items():
                others = [u for u in users.get(key, ()) if not (u[0] == i0 and u[1] == fi0)]
                if not others:
                    continue
                kill.add((i0, fi0))
                for i, fi, k in others:
                    f = faces_of[i][fi]
                    if mid in f:
                        if set(f) <= set(key) | {mid}:
                            kill.add((i, fi))                 # (the same triangle of no area again, turned the other way: the two go)
                        continue
                    inserts.setdefault((i, fi), []).append((k, mid))
            if not kill:
                break
            for (i, fi), lst in inserts.items():
                f = list(faces_of[i][fi])
                for k, mid in sorted(lst, reverse=True):
                    f.insert(k + 1, mid)
                faces_of[i][fi] = tuple(f)
                changed.add(i)
            for i in set(i for i, _ in kill):
                faces_of[i] = [f for fi, f in enumerate(faces_of[i]) if (i, fi) not in kill]
                changed.add(i)
        keys = [[tuple(sorted(f)) for f in fl] for fl in faces_of]
        holders = {}
        for i, ks in enumerate(keys):
            for k in ks:
                holders.setdefault(k, []).append(i)
        parent = list(range(len(faces_of)))

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        group = {}                                            # (the faces, key -> nodes, of a cell that has been joined to another or looked at)
        gone = set()

        def faces_dict(r):
            if r not in group:
                group[r] = dict(zip(keys[r], faces_of[r]))
            return group[r]

        # (a cut cell that is a wedge -- a hexahedron, prism or pyramid with an edge that has all but gone, scaled Jacobian under 0.2 -- is joined as well, whatever it holds: a row of them stood along a ridge of the bracket
        # at 2 mm, scaled Jacobian 0.05, each of a half cube)
        flat = array('d')
        for p in self.pos:
            flat.extend(p)
        poor = set()
        for i in range(count, len(faces_of)):
            if volume[i] > limit:
                kind, nodes = self._named(faces_of[i])
                if kind != 'poly' and self.M._cell_jacobian(flat, kind, nodes) < 0.2:
                    poor.add(i)
        for i in sorted((i for i in range(count, len(faces_of)) if volume[i] <= limit or i in poor), key=lambda i: volume[i]):
            r = find(i)
            force = i in poor
            for _ in range(8):
                if volume[r] > limit and not force:
                    break
                force = False
                # (the neighbour it shares the most face with, one that does not grow past two cubes by it when there is such a neighbour: the neighbour with the larger volume let one cell take every small one
                # along a ridge, a cell of ten cubes)
                shared = {}
                for k, f in faces_dict(r).items():
                    for j in holders.get(k, ()):
                        rj = find(j)
                        if rj != r:
                            shared[rj] = shared.get(rj, 0.0) + self._area(f)
                if not shared:
                    if volume[r] <= limit:
                        gone.add(r)                           # (no cell next to it: it is left out)
                    break
                best = max(shared, key=lambda x: (volume[x] + volume[r] <= 2.2 * cube, shared[x], -volume[x]))
                target = faces_dict(best)
                for k, f in group.pop(r).items():
                    if k in target:
                        del target[k]                         # (a face the two cells shared: it is inside the joined cell now)
                    else:
                        target[k] = f
                parent[r] = best
                volume[best] += volume[r]
                self.merged += 1
                r = best
        out = []
        for i in range(len(faces_of)):
            if find(i) != i:
                continue
            if i in gone:
                self.dropped += 1
                continue
            if i < count and i not in group and i not in changed:
                out.append(('hex8', hexes[i]))
            else:
                out.append(self._named(list(group[i].values()) if i in group else faces_of[i]))
        return out

    def _normals(self):
        """ The direction of the surface at every crossing: the gradient of the field, central differences over a hundredth of a cube.  A crossing that lies ON an edge of the body has no one direction (the
            differences before and after it differ): it has none here, and no edge node is made from it (it is a node of the edge already) """
        ids = sorted(set(self.cross.values()))
        h = 0.01 * self.size
        pts = []
        for g in ids:
            p = self.pos[g]
            pts.append(p)
            for axis in range(3):
                for sign in (-h, h):
                    q = list(p)
                    q[axis] += sign
                    pts.append(tuple(q))
        vals = self.M._eval(self.body, pts)
        for m, g in enumerate(ids):
            v = vals[7 * m:7 * m + 7]
            f0 = v[0]
            back = [(f0 - v[1 + 2 * c]) / h for c in range(3)]
            fore = [(v[2 + 2 * c] - f0) / h for c in range(3)]
            gx, gy, gz = [(back[c] + fore[c]) * 0.5 for c in range(3)]
            ln = math.sqrt(gx * gx + gy * gy + gz * gz)
            jump = math.sqrt(sum((fore[c] - back[c]) ** 2 for c in range(3)))
            self.normal[g] = (gx / ln, gy / ln, gz / ln) if ln > 1e-12 and jump < 0.25 * ln else None
            self.gradient_length[g] = ln

    def _edge_node(self, face_key, a, b):
        """ The node on an edge of the body where the cut across a face of the grid, from the crossing a to the crossing b, is bent: the cut's two sides are lines in the plane of the face, with the
            directions that are across the surface normals there; the node is where they meet (None when the normals differ by less than the edge angle, or the lines do not meet near the cut) """
        ck = (face_key, min(a, b), max(a, b))
        if ck in self.edge_node:
            return self.edge_node[ck]
        found = None
        na, nb = self.normal.get(a), self.normal.get(b)
        grid_nodes = (self.n[0] + 1) * (self.n[1] + 1) * (self.n[2] + 1)
        # (a crossing at a node of the grid is where the surface runs through the node: the direction of the surface there is not the direction of one side of an edge it may lie on)
        if a >= grid_nodes and b >= grid_nodes and na is not None and nb is not None and na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2] < self.cos_edge:
            # (a surface that curves fast enough turns by that much between the crossings too: a crease is where the cut between them goes round a corner, so that the middle of the straight line between
            # them is far from the surface -- more than a seventh of its length; for a hole of radius 2 in cubes of 1.5 it is a ninth)
            A0, B0 = self.pos[a], self.pos[b]
            span = math.dist(A0, B0)
            mid_point = tuple((A0[c] + B0[c]) * 0.5 for c in range(3))
            scale = 0.5 * (self.gradient_length.get(a, 1.0) + self.gradient_length.get(b, 1.0))
            if span <= 0.0 or abs(self.M._eval(self.body, [mid_point])[0]) / scale < 0.15 * span:
                self.edge_node[ck] = None
                return None
            corners = [self.pos[g] for g in face_key]
            axis = None
            for c in range(3):
                if max(q[c] for q in corners) - min(q[c] for q in corners) < 1e-9 * self.size:
                    axis = c
            if axis is not None:
                u, v = [c for c in range(3) if c != axis]
                A, B = self.pos[a], self.pos[b]
                g1 = (na[u], na[v])
                g2 = (nb[u], nb[v])
                l1 = math.hypot(*g1)
                l2 = math.hypot(*g2)
                if l1 > 1e-6 and l2 > 1e-6:
                    t1 = (-g1[1] / l1, g1[0] / l1)
                    t2 = (-g2[1] / l2, g2[0] / l2)
                    w = (B[u] - A[u], B[v] - A[v])
                    det = -(t1[0] * t2[1] - t1[1] * t2[0])
                    length = math.hypot(*w)
                    if abs(det) > 1e-3 and length > 0.0:
                        s_ = (w[0] * (-t2[1]) - (-t2[0]) * w[1]) / det
                        c2 = (A[u] + s_ * t1[0], A[v] + s_ * t1[1])
                        mid = ((A[u] + B[u]) * 0.5, (A[v] + B[v]) * 0.5)
                        if (math.hypot(c2[0] - mid[0], c2[1] - mid[1]) <= length and math.hypot(c2[0] - A[u], c2[1] - A[v]) > 0.05 * self.size
                                and math.hypot(c2[0] - B[u], c2[1] - B[v]) > 0.05 * self.size):
                            lo_u, hi_u = min(q[u] for q in corners), max(q[u] for q in corners)
                            lo_v, hi_v = min(q[v] for q in corners), max(q[v] for q in corners)
                            c2 = (min(max(c2[0], lo_u), hi_u), min(max(c2[1], lo_v), hi_v))
                            point = [0.0, 0.0, 0.0]
                            point[axis] = A[axis]
                            point[u], point[v] = c2
                            self.pos.append(tuple(point))
                            found = len(self.pos) - 1
                            self.edge_ids.add(found)
                            self.edge_axis[found] = axis
        self.edge_node[ck] = found
        return found

    def _x(self, ids, a, b):
        ga, gb = ids[a], ids[b]
        return self.cross[(ga, gb) if ga < gb else (gb, ga)]

    def _cut_cell(self, ids):
        ''' The faces (lists of nodes, turned outward) of the polyhedron of what is inside a cut cube, one list for each separate piece '''
        inside = self.inside
        s = [inside[g] for g in ids]
        faces = []
        segments = []                       # (the cuts across the faces that have an area, in the direction the face runs along them)
        loose = []                          # (the cuts across a face that has none: the surface goes through two of its corners)
        for quad in FACE:
            items = []
            for k in range(4):
                a, b = quad[k], quad[(k + 1) % 4]
                if s[a]:
                    items.append((ids[a], False))
                if s[a] != s[b]:
                    items.append((self._x(ids, a, b), True))
            if not items:
                continue
            crossings = 0
            for _, flag in items:
                crossings += flag
            polys = []
            if crossings <= 2:
                polys.append(items)
            else:
                key = tuple(sorted(ids[c] for c in quad))
                if self.centre.get(key, False):
                    polys.append(items)
                else:
                    m = len(items)
                    for p in range(m):
                        if not items[p][1]:
                            polys.append([items[(p - 1) % m], items[p], items[(p + 1) % m]])
            for poly in polys:
                verts = []
                flags = []
                for v, flag in poly:
                    if verts and verts[-1] == v:
                        flags[-1] = flags[-1] or flag
                        continue
                    verts.append(v)
                    flags.append(flag)
                if len(verts) > 1 and verts[0] == verts[-1]:
                    verts.pop()
                    flags[0] = flags[0] or flags.pop()
                if len(verts) == 2 and flags[0] and flags[1]:
                    loose.append((verts[0], verts[1]))
                    continue
                if len(verts) < 3:
                    continue
                if self.edges and crossings == 2:
                    # (a cut across the face that is bent at an edge of the body: the node on the edge between its two crossings)
                    m = len(verts)
                    pairs = [p for p in range(m) if flags[p] and flags[(p + 1) % m] and verts[p] != verts[(p + 1) % m]]
                    if len(pairs) == 1:
                        p = pairs[0]
                        node = self._edge_node(tuple(sorted(ids[c] for c in quad)), verts[p], verts[(p + 1) % m])
                        if node is not None:
                            verts.insert(p + 1, node)
                            flags.insert(p + 1, True)
                faces.append(verts)
                m = len(verts)
                for p in range(m):
                    if flags[p] and flags[(p + 1) % m] and verts[p] != verts[(p + 1) % m]:
                        segments.append((verts[p], verts[(p + 1) % m]))
        # the polygons of the surface: the cuts chained into loops, each turned against the way the faces run along it
        adj = {}
        for u, v in segments + loose:
            adj.setdefault(u, []).append(v)
            adj.setdefault(v, []).append(u)
        directed = set(segments)
        used = set()
        loops = []
        for start in adj:
            if start in used:
                continue
            loop = [start]
            used.add(start)
            prev = None
            cur = start
            while True:
                nxt = None
                for w in adj[cur]:
                    if w != prev and w not in used:
                        nxt = w
                        break
                if nxt is None:
                    break
                loop.append(nxt)
                used.add(nxt)
                prev, cur = cur, nxt
            if len(loop) < 3:
                continue
            agree = disagree = 0
            for m in range(len(loop)):
                x, y = loop[m], loop[(m + 1) % len(loop)]
                if (y, x) in directed:
                    agree += 1
                elif (x, y) in directed:
                    disagree += 1
            if disagree > agree:
                loop.reverse()
            loops.append(loop)
        if self.edges and self.edge_ids:
            # (the polygon of the surface in a cell that an edge runs through: cut in two along the line between the two nodes on the edge)
            split = []
            for loop in loops:
                at = [m for m, v in enumerate(loop) if v in self.edge_ids]
                if len(at) == 2 and at[1] - at[0] >= 2 and len(loop) - (at[1] - at[0]) >= 2:
                    a_, b_ = at
                    split.append(loop[a_:b_ + 1])
                    split.append(loop[b_:] + loop[:a_ + 1])
                else:
                    split.append(loop)
            loops = split
        faces.extend(loops)
        pieces = self._pieces(faces)
        if loops:
            loop_ids = set(id(l) for l in loops)
            for piece in pieces:
                if self.volume(piece) < 0.0:             # (no face ran along the cuts to say which way the loops go: the way that makes the volume positive)
                    for m in range(len(piece)):
                        if id(piece[m]) in loop_ids:
                            piece[m] = piece[m][::-1]
        return pieces

    @staticmethod
    def _pieces(faces):
        ''' The faces apart by the pieces they close: two inside parts of a cube that touch at a node or not at all are two cells '''
        if len(faces) < 5:
            return [faces] if faces else []
        parent = list(range(len(faces)))

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        owner = {}
        for fi, fc in enumerate(faces):
            m = len(fc)
            for p in range(m):
                e = (fc[p], fc[(p + 1) % m]) if fc[p] < fc[(p + 1) % m] else (fc[(p + 1) % m], fc[p])
                if e in owner:
                    a, b = find(fi), find(owner[e])
                    if a != b:
                        parent[a] = b
                else:
                    owner[e] = fi
        groups = {}
        for fi in range(len(faces)):
            groups.setdefault(find(fi), []).append(faces[fi])
        return list(groups.values())

    def volume(self, faces):
        ''' The volume of the polyhedron of `faces`, from one of its own corners: the faces of a closed cell give the same volume from any point, and the flat piece that a surface lying ON a face of the grid leaves in a cube
            (two quadrilaterals on one plane, no thickness: it is not closed) gives none from a point of that plane.  From the origin of the coordinates it gave the size of the body's distance from the origin times the area of
            the piece (a plate 120 mm along y: 50 such pieces of 178 mm3 each, kept by the threshold as cells -- the cartesian mesh said 13,388 mm3 for 4,498 -- and, with quanta, taken for cubes the surface cuts: 50 cubes
            too many in the hexahedra of the same plate, 4.1 % over the volume) '''
        pos = self.pos
        r = pos[faces[0][0]]
        total = 0.0
        for fc in faces:
            p0 = pos[fc[0]]
            a = (p0[0] - r[0], p0[1] - r[1], p0[2] - r[2])
            for m in range(1, len(fc) - 1):
                p1, p2 = pos[fc[m]], pos[fc[m + 1]]
                total += _dot(a, _cross((p1[0] - r[0], p1[1] - r[1], p1[2] - r[2]), (p2[0] - r[0], p2[1] - r[1], p2[2] - r[2]))) / 6.0
        return total

    def _named(self, faces):
        ''' (kind, nodes) of the polyhedron: by its nodes and faces, with the nodes in the order of the tables of meshing.py '''
        pos = self.pos
        edges = {}
        verts = []
        for fc in faces:
            m = len(fc)
            for p in range(m):
                a, b = fc[p], fc[(p + 1) % m]
                edges.setdefault(a, set()).add(b)
                edges.setdefault(b, set()).add(a)
                if a not in verts:
                    verts.append(a)
        sizes = sorted(len(fc) for fc in faces)
        nv = len(verts)
        if nv == 4 and sizes == [3, 3, 3, 3]:
            t = verts
            ux = [pos[t[1]][c] - pos[t[0]][c] for c in range(3)]
            vx = [pos[t[2]][c] - pos[t[0]][c] for c in range(3)]
            wx = [pos[t[3]][c] - pos[t[0]][c] for c in range(3)]
            if _dot(ux, _cross(vx, wx)) < 0.0:
                t = [t[0], t[2], t[1], t[3]]
            return ('tet4', t)
        if nv == 8 and sizes == [4] * 6:
            a = faces[0]
            bottom = [a[0], a[3], a[2], a[1]]
            top = []
            for b in bottom:
                other = [x for x in edges[b] if x not in bottom]
                if len(other) != 1:
                    return ('poly', faces)
                top.append(other[0])
            return ('hex8', bottom + top)
        if nv == 5 and sizes == [3, 3, 3, 3, 4]:
            base = [fc for fc in faces if len(fc) == 4][0]
            apex = [x for x in verts if x not in base][0]
            return ('pyra5', [base[0], base[3], base[2], base[1], apex])
        if nv == 6 and sizes == [3, 3, 4, 4, 4]:
            tri = [fc for fc in faces if len(fc) == 3]
            p, q, r = tri[0]
            bottom = [p, r, q]                               # (n0, n1, n2: the face (n0, n2, n1) is the one turned outward)
            top = []
            for b in bottom:
                other = [x for x in edges[b] if x not in bottom]
                if len(other) != 1:
                    return ('poly', faces)
                top.append(other[0])
            return ('prism6', bottom + top)
        return ('poly', faces)
