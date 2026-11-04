'''
Checks of editable_polyhedron (fieldes.stdlib.mesh_edit): a triangle mesh as a body of points, and of the import of a mesh file as one (app_support.import_editable).  Library checks, no window.
Run with FIELDES_DIR set to the folder of the program and PYTHONPATH to python/.
'''
import json
import os
import struct
import sys
import tempfile
import traceback

from fieldes import *
from fieldes.stdlib import mesh_edit

bad = []
folder = tempfile.mkdtemp(prefix='mesh_edit_checks_')


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def inside(s, p):
    return evaluate(s, [p])[0] < 0


def corners(lo, hi):
    return [(lo[0] if not i & 1 else hi[0], lo[1] if not i & 2 else hi[1], lo[2] if not i & 4 else hi[2]) for i in range(8)]


QUADS = ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5))


def cube_triangles(lo=(0, 0, 0), hi=(40, 30, 20), outward=True):
    c = corners(lo, hi)
    out = []
    for q in QUADS:
        a, b, d, e = c[q[0]], c[q[1]], c[q[2]], c[q[3]]
        out += [(a, b, d), (a, d, e)] if outward else [(a, d, b), (a, e, d)]
    return out


def write_stl(name, triangles, binary=False):
    path = os.path.join(folder, name)
    if binary:
        with open(path, 'wb') as f:
            f.write(b'\0' * 80 + struct.pack('<I', len(triangles)))
            for t in triangles:
                f.write(struct.pack('<12fH', 0, 0, 0, *[c for p in t for c in p], 0))
    else:
        with open(path, 'w') as f:
            f.write('solid x\n')
            for t in triangles:
                f.write('facet normal 0 0 0\n outer loop\n' + ''.join('  vertex %r %r %r\n' % p for p in t) + ' endloop\nendfacet\n')
            f.write('endsolid x\n')
    return path


def is_box(body):
    return (inside(body, (20, 15, 10)) and not inside(body, (-0.5, 15, 10)) and not inside(body, (40.5, 15, 10)) and not inside(body, (20, 30.5, 10)) and not inside(body, (20, 15, 20.5))
            and inside(body, (0.3, 0.3, 0.3)) and inside(body, (39.7, 29.7, 19.7)) and abs(evaluate(body, [(40, 15, 10)])[0]) < 0.05)


def run(fn):
    try:
        fn()
    except Exception:
        traceback.print_exc()
        bad.append(fn.__name__ + ' raised')


def cubes():
    for label, path in (('a text STL', write_stl('cube_text.stl', cube_triangles())), ('a binary STL', write_stl('cube_binary.stl', cube_triangles(), binary=True)),
                        ('an STL that faces inward', write_stl('cube_inward.stl', cube_triangles(outward=False)))):
        verts, faces = mesh_edit.mesh_faces(path)
        check('%s of a box: 8 points and its twelve triangles (a tri mesh stays a tri mesh: no face of four)' % label, len(verts) == 8 and len(faces) == 12 and all(len(f) == 3 for f in faces), (len(verts), [len(f) for f in faces]))
        check('%s: editable_polyhedron of it is the box' % label, is_box(editable_polyhedron(path)))
    check('a morph_body of the same mesh goes through its points', abs(evaluate(editable_polyhedron(write_stl('cube_text.stl', cube_triangles()), smooth=True), [(0, 0, 0)])[0]) < 0.3)
    obj = os.path.join(folder, 'cube.obj')
    with open(obj, 'w') as f:
        for p in corners((0, 0, 0), (40, 30, 20)):
            f.write('v %r %r %r\n' % p)
        for q in QUADS:
            f.write('f %d/1 %d/1 %d/1 %d/1\n' % tuple(i + 1 for i in q))
    verts, faces = mesh_edit.mesh_faces(obj)
    check('an OBJ with squares: 8 points, its squares cut in triangles: 12 faces', len(verts) == 8 and len(faces) == 12, (len(verts), len(faces)))
    check('... and it is the box', is_box(editable_polyhedron(obj)))
    big = write_stl('cube_big.stl', cube_triangles(lo=(0, 0, 0), hi=(40000, 30000, 20000)))
    check('the units: a file in metres read as millimetres is a thousand times bigger (file_units="m")', abs(max(v[0] for v in mesh_edit.mesh_faces(big, 'mm', 'm')[0]) - 40000000.0) < 1.0)


def octahedron():
    p = [(20, 0, 0), (-20, 0, 0), (0, 20, 0), (0, -20, 0), (0, 0, 20), (0, 0, -20)]
    faces = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
    path = write_stl('octahedron.stl', [(p[a], p[b], p[c]) for a, b, c in faces])
    verts, f = mesh_edit.mesh_faces(path)
    check('an octahedron: 6 points, 8 faces of three', len(verts) == 6 and len(f) == 8 and all(len(x) == 3 for x in f), (len(verts), len(f)))
    body = editable_polyhedron(path)
    check('... and it is the octahedron (inside near the middle and near a point, outside beyond the faces)', inside(body, (0, 0, 0)) and inside(body, (18, 0, 0)) and not inside(body, (12, 12, 12)) and not inside(body, (21, 0, 0)))


def refusals():
    tris = cube_triangles()
    try:
        mesh_edit.mesh_faces(write_stl('open.stl', tris[2:]))
        refused = ''
    except ValueError as e:
        refused = str(e)
    check('a mesh with a hole is refused, and the message says it is not closed', 'not a closed mesh' in refused, refused[:100])
    try:
        mesh_edit.mesh_faces(write_stl('cube_text.stl', tris), max_points=3)
        refused = ''
    except ValueError as e:
        refused = str(e)
    check('a mesh of more faces than max_points is refused, and the message says how many', 'more than max_points=3' in refused and '12 faces' in refused, refused[:120])
    try:
        mesh_edit.mesh_faces(os.path.join(folder, 'nothing.stl'))
        refused = ''
    except ValueError as e:
        refused = str(e)
    check('a file that is not there is an error that says so', 'is not there' in refused, refused[:100])


def importing():
    ''' File > Import model of a triangle mesh (the user, 2026-11-02, H64: "a tri mesh should import as a polyhedron"): the Python side of it, app_support.import_editable, which the program calls '''
    from array import array
    from fieldes import app_support
    from fieldes.stdlib import mesh_import as mi

    def imported(path, name='part', file_units='mm'):
        return json.loads(app_support.import_editable(json.dumps({'path': path, 'name': name, 'file_units': file_units})))
    made = imported(write_stl('import_cube.stl', cube_triangles()))
    text = made.get('text', '')
    lines = text.split('\n')
    at = [k for k, line in enumerate(lines) if line.startswith('part = morph_polyhedron(')]
    # (the faces list ends with the line "# fieldes-internal: end", which the program reads to find where the list stops: it is the line directly above the call)
    check('import: a closed mesh comes in as a body of points: eight points, the faces list directly above the call, the morph_polyhedron, the line that shows it',
          made.get('points') == 8 and text.count('= point(') == 8 and len(at) == 1 and lines[at[0] - 1].startswith('# fieldes-internal: end') and lines[at[0] - 2] == ']'
          and any(line.startswith('part_faces = [') for line in lines[:at[0]]) and text.rstrip().endswith('\npart'), made.get('reason', text[:60]))
    # (one global position: every point stands where it is, so the eight positions are the eight corners, none twice)
    import re
    positions = re.findall(r'position=\((var\([^()]*\), var\([^()]*\), var\([^()]*\))\)', text)
    check('import: each point is a model of its own with its gizmo (position, rotation and scale): eight different positions, eight rotations of zero, eight scales of one',
          len(positions) == 8 and len(set(positions)) == 8 and text.count('rotation=(var(0), var(0), var(0))') == 8 and text.count('scale=(var(1), var(1), var(1))') == 8, str(positions[:2]))
    ns = {'var': lambda v: v}
    exec('from fieldes import *\n' + text, ns)
    check('import: the script it writes runs and is the box', is_box(ns['part']))
    hole = imported(write_stl('import_open.stl', cube_triangles()[2:]))
    check('import: a mesh with a hole stays a mesh -- no text, and the reason says it is not closed, without the name of a function', 'text' not in hole and 'not a closed mesh' in hole.get('reason', '') and not hole['reason'].startswith('editable_polyhedron'), hole.get('reason', '')[:100])
    huge = imported(write_stl('import_huge.stl', cube_triangles() * 3334))
    check('import: a mesh of far too many triangles stays a mesh, and says so without welding them', 'text' not in huge and 'far more' in huge.get('reason', ''), huge.get('reason', '')[:100])
    nothing = imported(os.path.join(folder, 'nothing.stl'))
    check('import: a file that is not there is a reason, not a crash', 'text' not in nothing and 'is not there' in nothing.get('reason', ''), nothing.get('reason', '')[:100])
    # (write_glb takes millimetres and writes metres, as a GLB has them: the box of 40 x 30 x 20 millimetres is 0.04 x 0.03 x 0.02 in the file, and comes back as 20 x 30 x 40 millimetres, whichever way the axes are turned)
    index, xyz, tri = {}, array('f'), array('I')
    for t in cube_triangles():
        for p in t:
            if p not in index:
                index[p] = len(index)
                xyz.extend(p)
            tri.append(index[p])
    glb = os.path.join(folder, 'import_cube.glb')
    mi._meshfiles.write_glb(glb, xyz, tri)
    verts, faces = mesh_edit.mesh_faces(glb)
    sizes = sorted(max(v[a] for v in verts) - min(v[a] for v in verts) for a in range(3))
    check('import: a GLB is read, in metres, into millimetres: eight points, twelve triangles, 20 x 30 x 40', len(verts) == 8 and len(faces) == 12 and all(abs(s - w) < 1e-3 for s, w in zip(sizes, (20, 30, 40))), (len(verts), len(faces), sizes))
    glbs = imported(glb, file_units=None)
    check('import: and File > Import model of that GLB makes a body of eight points', glbs.get('points') == 8, glbs.get('reason', ''))


for fn in (cubes, octahedron, refusals, importing):
    run(fn)
print('=' * 70)
print('FAILED: ' + ', '.join(bad) if bad else 'ALL CHECKS PASSED')
sys.exit(1 if bad else 0)