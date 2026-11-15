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
import os
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
        self.on_edge = set()                                  # (the nodes that the edge pass put on an edge of the body)
        self.edge_moved = 0                                   # (how many moves of that kind the edge pass made, all rounds)
        self.edge_stats = {}                                  # (what the edge pass did and why it refused: counted, for the probes)
        self.collapsed = 0                                    # (the nodes that were taken into a node beside them, _weld)

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
        self.welded = 0                                       # (the edge nodes that were one node with another at the same place)
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
            if self.edges and os.environ.get('FIELDES_CART_EDGES') != '0':
                self._weld(out)
                self._edge_pass(out)
                self._weld(out)
        self.cells = out
        return out

    def _edge_pass(self, cells, rounds=4):
        """ THE EDGE PASS OF THE TETRAHEDRA, for the cut cells (H223, his order of 2026-11-15: "YOU OBVIOUSLY KONW WHAT YOU DID TO MAKE TET MESHING AS GOOD AS IT IS. OBEY MY ORDER AND IMPLEMENT THAT"; the kernel's
            snapFeatures): the faces of the skin that lean away from the field (their normal against the field's direction in their middle more than 12 degrees) name the nodes to look at.  The faces round such a node that do
            NOT lean say which planes meet there (their normals, in clusters of 40 degrees, one plane each: the planes of the two sides of an edge); the node is moved to the point of the line where two of them meet that is
            nearest to it, and put back on the surface along the field's own direction; it stays where it is when that point is not on the surface or is farther than 0.6 of a cube.  (The rim of a flat face, the kernel's
            snapRims -- the node moved along the plane of the flat face onto the wall, the field read a quarter, an eighth, a sixteenth of a cube inside the body -- was tried here and made the leaning faces MORE
            numerous, 245 -> 343 of 16,500 on the bracket at 2 mm: it is not in.)  The moves are made ONE NODE AT A TIME against the cells as they are: a move that leaves a cell with a volume under 0.7 of what it had, or under the smallest cell that the join leaves (a tenth of a cube) when it had more, or a
            hexahedron, prism, pyramid or tetrahedron with a scaled Jacobian that is worse than 0.3 (or than 0.9 of what it was), or a face of a cell that turns over or is squeezed under a quarter of its area, is tried again at half the way (put back on the surface) and then taken back.  Up to
            `rounds` rounds.  The pass is wrapped in _weld (the collapse of nodes that stand within a tenth of a cube of each other).  FIELDES_CART_EDGES=0 turns it off """
        M = self.M
        pos = self.pos
        size = self.size
        cos_crease = math.cos(math.radians(40.0))
        cos_lean = math.cos(math.radians(12.0))               # (4 degrees was tried: it put a few more nodes of the neck edge on the crease and made 6 faces turn the wrong way and the leaning faces more)
        cfaces = []
        for kind, data in cells:
            cfaces.append([tuple(f) for f in data] if kind == 'poly' else [tuple(data[j] for j in face) for face in M.FACES[kind]])
        node_cells = {}
        for ci, fl in enumerate(cfaces):
            for x in {x for f in fl for x in f}:
                node_cells.setdefault(x, []).append(ci)
        flat = array('d')
        for p in pos:
            flat.extend(p)

        def volume_of(ci):
            kind, data = cells[ci]
            return self.volume(data) if kind == 'poly' else M._cell_volume(flat, kind, data)
        vol0 = [volume_of(ci) for ci in range(len(cells))]
        jac0 = {ci: M._cell_jacobian(flat, kind, data) for ci, (kind, data) in enumerate(cells) if kind != 'poly'}

        floor = self.d[0] * self.d[1] * self.d[2] / self.threshold          # (the smallest cell that the join of the small ones leaves: a move does not push a cell under it)

        stats = self.edge_stats = {}

        def tally(key, n=1):
            stats[key] = stats.get(key, 0) + n

        def cell_why(ci):
            """ None when the cell is sound, else what is wrong with it """
            kind, data = cells[ci]
            v = volume_of(ci)
            if not v > 0.7 * vol0[ci]:
                return 'cell under 0.7 of its volume'
            if v < min(vol0[ci], floor):
                return 'cell under a tenth of a cube'
            if kind != 'poly' and M._cell_jacobian(flat, kind, data) < min(0.3, 0.9 * jac0[ci]):
                return 'scaled Jacobian'
            return None

        def avec(face):
            p0 = pos[face[0]]
            sx = sy = sz = 0.0
            for m in range(1, len(face) - 1):
                p1, p2 = pos[face[m]], pos[face[m + 1]]
                ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
                vx, vy, vz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
                sx += uy * vz - uz * vy
                sy += uz * vx - ux * vz
                sz += ux * vy - uy * vx
            return (0.5 * sx, 0.5 * sy, 0.5 * sz)

        h = 0.01 * size
        def layout(q):
            """ The points where the field is read for a Newton step of the node q: q and its six neighbours along the axes """
            return [q] + [tuple(q[c] + (sg if c == axis else 0.0) for c in range(3)) for axis in range(3) for sg in (-h, h)]

        def advance(q, v):
            """ One Newton step of q onto the surface from the values v at layout(q): along the gradient of the field """
            g = [(v[2 + 2 * c] - v[1 + 2 * c]) / (2.0 * h) for c in range(3)]
            ln2 = g[0] * g[0] + g[1] * g[1] + g[2] * g[2]
            if ln2 < 1e-12:
                return q
            return (q[0] - v[0] / ln2 * g[0], q[1] - v[0] / ln2 * g[1], q[2] - v[0] / ln2 * g[2])
        def compact(face, a):
            """ How compact a face is: its area over the square of its longest edge (a square 1, an equilateral triangle 0.43, a sliver of three nodes nearly on a line 0.01) """
            longest = 0.0
            for k in range(len(face)):
                p, q = pos[face[k]], pos[face[(k + 1) % len(face)]]
                longest = max(longest, (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2)
            return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) / longest if longest > 0.0 else 0.0

        def put(nd, new):
            """ The node nd to `new` when every cell round it stays sound (cell_ok) and no face of those cells turns over or is squeezed to under a quarter of its area (a node put onto the line between its neighbours makes
                a triangle of no area); otherwise it stays where it was.  True when it moved """
            old = pos[nd]
            fa = [f for ci in node_cells.get(nd, ()) for f in cfaces[ci] if nd in f]
            old_av = [avec(f) for f in fa]
            old_shape = [compact(f, a) for f, a in zip(fa, old_av)]
            pos[nd] = new
            flat[3 * nd:3 * nd + 3] = array('d', new)
            why = None
            for ci in node_cells.get(nd, ()):
                why = cell_why(ci)
                if why:
                    break
            if not why:
                for f, a, q in zip(fa, old_av, old_shape):
                    b = avec(f)
                    la = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
                    lb = math.sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2])
                    if lb < 0.25 * la or a[0] * b[0] + a[1] * b[1] + a[2] * b[2] < 0.25 * la * lb:
                        why = 'a face turned over or squeezed'
                        break
                    shape = compact(f, b)
                    if shape < 0.03 and shape < q:
                        why = 'a face made a sliver (three nodes nearly on a line)'
                        break
            sound = not why
            if not sound:
                tally('refused: ' + why)
                pos[nd] = old
                flat[3 * nd:3 * nd + 3] = array('d', old)
            return sound
        self.edge_moved = 0
        for rnd in range(rounds):
            count = {}
            for fl in cfaces:
                for f in fl:
                    e = count.setdefault(tuple(sorted(f)), [0, f])
                    e[0] += 1
            skin = [e[1] for e in count.values() if e[0] == 1]
            mids, norms, areas = [], [], []
            for f in skin:
                a = avec(f)
                ln = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
                areas.append(ln)
                norms.append((a[0] / ln, a[1] / ln, a[2] / ln) if ln > 0.0 else None)
                mids.append(tuple(sum(pos[x][c] for x in f) / len(f) for c in range(3)))
            pts = []
            for (x, y, z) in mids:
                pts.extend(((x + h, y, z), (x - h, y, z), (x, y + h, z), (x, y - h, z), (x, y, z + h), (x, y, z - h)))
            vals = M._eval(self.body, pts)
            lean = []
            for i, n in enumerate(norms):
                v = vals[6 * i:6 * i + 6]
                g = ((v[0] - v[1]) / (2 * h), (v[2] - v[3]) / (2 * h), (v[4] - v[5]) / (2 * h))
                gl = math.sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2])
                lean.append(n is not None and gl > 1e-9 and abs(n[0] * g[0] + n[1] * g[1] + n[2] * g[2]) / gl < cos_lean)
            skin_of = {}
            for i, f in enumerate(skin):
                for x in f:
                    skin_of.setdefault(x, []).append(i)
            candidates = sorted({x for i, f in enumerate(skin) if lean[i] for x in f})
            if not candidates:
                break
            tally('round %d: nodes of leaning faces' % rnd, len(candidates))
            targets = []
            for nd in candidates:
                near = set()
                for i in skin_of[nd]:
                    near.update(skin[i])
                faces_here = {i for x in near for i in skin_of[x] if not lean[i] and norms[i] is not None}
                clusters = []
                for i in sorted(faces_here, key=lambda i: -areas[i]):
                    n = norms[i]
                    for cl in clusters:
                        m = cl[0]
                        ml = math.sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2])
                        if (m[0] * n[0] + m[1] * n[1] + m[2] * n[2]) / ml > cos_crease:
                            cl[0] = (m[0] + areas[i] * n[0], m[1] + areas[i] * n[1], m[2] + areas[i] * n[2])
                            cl[1] += areas[i]
                            cl[2] = tuple(cl[2][c] + areas[i] * mids[i][c] for c in range(3))
                            break
                    else:
                        clusters.append([(areas[i] * n[0], areas[i] * n[1], areas[i] * n[2]), areas[i], tuple(areas[i] * mids[i][c] for c in range(3))])
                if len(clusters) < 2:
                    if rnd == 0:
                        tally('first round: no second plane beside the node (%d)' % len(clusters))
                    continue
                clusters.sort(key=lambda cl: -cl[1])
                planes = []
                for m, a_, c_ in clusters:
                    ml = math.sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2])
                    planes.append(((m[0] / ml, m[1] / ml, m[2] / ml), tuple(c / a_ for c in c_)))
                n1, c1 = planes[0]
                other = None
                for n2, c2 in planes[1:]:
                    if n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2] < cos_crease:
                        other = (n2, c2)
                        break
                if other is None:
                    if rnd == 0:
                        tally('first round: the planes beside the node do not differ by 40 degrees')
                    continue
                n2, c2 = other
                p = pos[nd]
                d12 = n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2]
                det = 1.0 - d12 * d12
                if det < 1e-3:
                    continue
                r1 = n1[0] * (p[0] - c1[0]) + n1[1] * (p[1] - c1[1]) + n1[2] * (p[2] - c1[2])
                r2 = n2[0] * (p[0] - c2[0]) + n2[1] * (p[1] - c2[1]) + n2[2] * (p[2] - c2[2])
                s1 = (r1 - d12 * r2) / det
                s2 = (r2 - d12 * r1) / det
                x = tuple(p[c] - s1 * n1[c] - s2 * n2[c] for c in range(3))
                if math.dist(x, p) <= 0.6 * size:
                    targets.append([nd, x])
                elif rnd == 0:
                    tally('first round: the edge is farther than 0.6 of a cube')
            if not targets:
                break
            if rnd == 0:
                tally('first round: nodes with a place on an edge', len(targets))
            # (put back on the surface along the field's own direction: a few Newton steps, all the nodes together, a call of the field for each)
            for _ in range(3):
                pts = []
                for t in targets:
                    pts.extend(layout(t[1]))
                vals = M._eval(self.body, pts)
                for m, t in enumerate(targets):
                    t[1] = advance(t[1], vals[7 * m:7 * m + 7])
            final = M._eval(self.body, [t[1] for t in targets])
            moved = 0
            for (nd, x), f in zip(targets, final):
                if abs(f) > 2e-3 * size or math.dist(x, pos[nd]) > 0.6 * size or math.dist(x, pos[nd]) < 1e-4 * size:
                    if rnd == 0:
                        tally('first round: the place is not on the surface (or is where the node is)')
                    continue
                old = pos[nd]
                for step in (1.0, 0.5):
                    new = tuple(old[c] + step * (x[c] - old[c]) for c in range(3))
                    if step < 1.0:
                        # (half the way along the line is off the surface by up to half the way: it is put back on the surface, and when it does not get there the node stays)
                        for _ in range(3):
                            new = advance(new, M._eval(self.body, layout(new)))
                        if abs(M._eval(self.body, [new])[0]) > 2e-3 * size:
                            continue
                    if put(nd, new):
                        moved += 1
                        self.on_edge.add(nd)
                        break
            self.edge_moved += moved
            if not moved:
                break

    def _weld(self, cells, tol=0.1):
        """ THE COLLAPSE OF THE TETRAHEDRA, for the cut cells (the kernel's collapseBites / collapseCorners: a node that has come to stand beside another is taken INTO it, not left as an edge of no length): every edge of
            a cell shorter than `tol` of the cube side, shortest first, is collapsed: the node that is not on a crease (a grid node) goes into the one that is (an edge node, or a node that the edge pass put on a crease), else
            the higher number into the lower; the kept node stays where it is.  The faces round the removed node are written again with the kept one (a face that has fewer than three nodes left is gone, a face that
            repeats a node away from its neighbour refuses the collapse); a cell that holds both nodes becomes the polyhedron that is left (named again: a hexahedron that lost an edge is a prism).  Refused when a cell round the
            node would be left with less than 0.4 of its volume or under the smallest cell that the join leaves (a tenth of a cube) when it had more (a cell that has under a tenth of a cube may vanish), or a
            hexahedron, prism or pyramid with a scaled Jacobian worse than 0.3 (or than 0.9 of what it was); two nodes under a fiftieth of a cube apart are collapsed whatever the cell looks like then (it stays a polyhedron) """
        M = self.M
        pos = self.pos
        size = self.size
        limit = tol * size
        cube = self.d[0] * self.d[1] * self.d[2]
        cf = []
        for kind, data in cells:
            cf.append([tuple(f) for f in data] if kind == 'poly' else [tuple(data[j] for j in face) for face in M.FACES[kind]])
        node_cells = {}
        for ci, fl in enumerate(cf):
            for x in {x for f in fl for x in f}:
                node_cells.setdefault(x, set()).add(ci)
        pairs = {}
        for fl in cf:
            for f in fl:
                n = len(f)
                for k in range(n):
                    a, b = f[k], f[(k + 1) % n]
                    if a != b:
                        key = (a, b) if a < b else (b, a)
                        if key not in pairs:
                            d = math.dist(pos[a], pos[b])
                            if d < limit:
                                pairs[key] = d
        if not pairs:
            return 0
        flat = array('d')
        for p in pos:
            flat.extend(p)
        on_edge = self.edge_ids | self.on_edge
        rep = {}

        def find(x):
            while x in rep:
                x = rep[x]
            return x
        done = 0
        for a0, b0 in sorted(pairs, key=pairs.get):
            a, b = find(a0), find(b0)
            if a == b or math.dist(pos[a], pos[b]) >= limit:
                continue
            ea, eb = a in on_edge, b in on_edge
            keep, gone = (a, b) if (ea and not eb) else ((b, a) if (eb and not ea) else ((a, b) if a < b else (b, a)))
            plan = []
            ok = True
            for ci in node_cells.get(gone, ()):
                if cells[ci] is None:
                    continue
                kind, data = cells[ci]
                old_volume = self.volume(cf[ci]) if kind == 'poly' else M._cell_volume(flat, kind, data)
                holds_both = keep in node_cells and ci in node_cells[keep]
                new_faces = []
                for f in cf[ci]:
                    g = [keep if x == gone else x for x in f]
                    h = [x for k, x in enumerate(g) if x != g[k - 1]]
                    if len(set(h)) < 3:
                        continue
                    if len(h) != len(set(h)):
                        ok = False
                        break
                    new_faces.append(tuple(h))
                if not ok:
                    break
                if len(new_faces) < 4:
                    if old_volume > 0.1 * cube:
                        ok = False
                        break
                    plan.append((ci, None, None))
                    continue
                if not holds_both and kind != 'poly':
                    new_data = [keep if x == gone else x for x in data]
                    new_kind = kind
                    new_volume = M._cell_volume(flat, kind, new_data)
                    jac_new = M._cell_jacobian(flat, kind, new_data)
                    jac_old = M._cell_jacobian(flat, kind, data)
                    if jac_new < min(0.3, 0.9 * jac_old):
                        ok = False
                        break
                else:
                    new_kind, new_data = self._named(new_faces)
                    new_volume = self.volume(new_faces)
                    if new_kind != 'poly' and M._cell_jacobian(flat, new_kind, new_data) < 0.3:
                        if math.dist(pos[keep], pos[gone]) >= 0.02 * size:
                            ok = False
                            break
                        # (two nodes at one place -- under a fiftieth of a cube apart -- are one node whatever the cell that is left looks like: the edge of no length is the worse fault, and the cell stays the polyhedron it is)
                        new_kind, new_data = 'poly', new_faces
                if not new_volume > 0.4 * old_volume or new_volume < min(old_volume, cube / self.threshold):
                    ok = False
                    break
                plan.append((ci, (new_kind, new_data), new_faces))
            if not ok:
                continue
            for ci, cell, new_faces in plan:
                if cell is None:
                    cells[ci] = None
                    cf[ci] = []
                    continue
                cells[ci] = cell
                cf[ci] = new_faces
                node_cells.setdefault(keep, set()).add(ci)
            node_cells[gone] = set()
            rep[gone] = keep
            done += 1
        if done:
            cells[:] = [c for c in cells if c is not None]
        self.collapsed += done
        return done

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
        # (two edge nodes at one place: a crease that runs along an edge of a cube of the grid is met by the cuts across both faces of the grid that share the edge, and each made a node there; they are one node, and a
        # face that is left with fewer than three nodes goes)
        tol = 0.005 * self.size
        bucket = {}
        canon = {}
        for g in sorted(g for g in used if g in self.edge_axis):
            p = self.pos[g]
            key = tuple(int(math.floor(c / (2.0 * tol))) for c in p)
            found = None
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for other in bucket.get((key[0] + dx, key[1] + dy, key[2] + dz), ()):
                            if found is None and math.dist(p, self.pos[other]) <= tol:
                                found = other
            if found is None:
                bucket.setdefault(key, []).append(g)
            else:
                canon[g] = found
        if canon:
            for i, (faces, v) in enumerate(pieces):
                new_faces = []
                for f in faces:
                    nf = []
                    for x in f:
                        x = canon.get(x, x)
                        if not nf or nf[-1] != x:
                            nf.append(x)
                    while len(nf) > 1 and nf[0] == nf[-1]:
                        nf.pop()
                    if len(set(nf)) >= 3:
                        new_faces.append(nf)
                pieces[i] = (new_faces, v)
            self.welded = len(canon)
        todo = [g for g in used if g in self.edge_axis and g not in canon]
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
        # (an end of the cut that is a node of the grid, one the snap took for on the surface, has a normal of its own and counts as well: it was left out as "the direction of the surface there is not the direction of
        # one side of an edge it may lie on", and 886 of the cuts across the creases of the real bracket at 2 mm ended in one, which got no edge node: the faces that lean more than 12 degrees fell from 6.1 % to 2.8 %)
        if na is not None and nb is not None and na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2] < self.cos_edge:
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
                            # (a point on a SIDE of the face is on an edge of the grid, which the cells on the other side of it share and do not have the node on: the polygon of this face would run out along the side to the
                            # corner and back to the new node, a spike of no width -- 164 faces of the real bracket at 2 mm, skewness 1 -- and the node would hang on that edge.  The crease that runs along a grid edge needs no node)
                            side = 0.02 * self.size
                            if min(c2[0] - lo_u, hi_u - c2[0], c2[1] - lo_v, hi_v - c2[1]) > side:
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
