'''
Checks of Shift+S with models selected (profile_edit.edit, op 'symmetry_add'): every selected model is added to the symmetry -- its images are made under it -- whatever it is and whether or not it
has a mirror partner (the user, 2026-11-03: "selecting a part an pressing shift s adds it to the current symmetry", "also for shapes that do not have a mirror point partner already", "with shape I
mean any body", "a morph shape or spline that does not have any mirror partner also gets mirrored").  Library checks, no window.  Run with FIELDES_DIR set to the folder of the program and
PYTHONPATH to python/.
'''
import json
import sys
import traceback

from fieldes import *
from fieldes import profile_edit, runner

bad = []


def check(label, ok, extra=''):
    print(('PASS  ' if ok else 'FAIL  ') + label + (('   ' + str(extra)) if extra != '' else ''))
    if not ok:
        bad.append(label)


def inside(s, p):
    return evaluate(s, [p])[0] < 0


def apply_edits(source, edits):
    lines = source.split('\n')
    for l0, c0, l1, c1, text in sorted(edits, key=lambda e: (e[0], e[1]), reverse=True):
        head = lines[l0][:c0]
        tail = lines[l1][c1:]
        lines[l0:l1 + 1] = (head + text + tail).split('\n')
    return '\n'.join(lines)


def make(source):
    ns = {'var': lambda v: v}
    exec('from fieldes import *\n' + source, ns)
    runner.last_globals = ns
    return ns


def display_line(script, name):
    return max(i for i, l in enumerate(script.split('\n'), 1) if l.strip() == name)


def symmetry_text(name):
    return ("%s = symmetry_planes(\n    planes=1,\n    kind='axis',\n    axis=(0, 0, 1),\n    first=(0, 1, 0),\n    position=(var(0), var(0), var(0)),\n    rotation=(var(0), var(0), var(0)),\n)\n%s\n"
            % (name, name))


def point_text(name, x, y, z):
    return ('%s = point(\n    position=(var(%s), var(%s), var(%s)),\n    rotation=(var(0), var(0), var(0)),\n    scale=(var(1), var(1), var(1)),\n)\n%s\n' % (name, x, y, z, name))


MIRROR = {'var': 'mirror', 'centre': [0, 0, 0], 'elements': [{'mirror': [1, 0, 0], 'turn': None}]}


def add(script, names, symmetry=MIRROR):
    make(script)             # (the program has run the script it edits: runner.last_globals is that run)
    request = {'op': 'symmetry_add', 'source': script, 'lines': [display_line(script, n) for n in names], 'symmetry': symmetry}
    return apply_edits(script, json.loads(profile_edit.edit(json.dumps(request)))['edits'])


def refused(script, names, word):
    try:
        add(script, names)
    except ValueError as e:
        return word in str(e)
    return False


def a_box():
    script = symmetry_text('mirror') + 'b = box((10, 10, 10), position=(25, 5, 5))\nb\n'
    done = add(script, ['b'])
    ns = make(done)
    check('a box: its images are made under it, distinct ones only', 'b_images = symmetry_images(b, mirror, distinct=True)' in done, done.split('\n')[-3:])
    check('... and the run has the box and its mirror image, on the other side of the plane', inside(ns['b'], (25, 5, 5)) and not inside(ns['b'], (-25, 5, 5)) and inside(ns['b_images'], (-25, 5, 5)),
          'images ' + str(type(ns['b_images']).__name__))
    # the symmetry made AFTER the model, as Shift+S makes one: the images go after it, so that the name exists when the line runs
    later = 'b = box((10, 10, 10), position=(25, 5, 5))\nb\n' + symmetry_text('mirror')
    done = add(later, ['b'])
    ns = make(done)
    check('the symmetry made after the box: the images line goes after the symmetry, once, and the script runs', done.count('mirror = symmetry_planes(') == 1
          and done.index('mirror = symmetry_planes(') < done.index('b_images = symmetry_images(') and inside(ns['b_images'], (-25, 5, 5)))
    check('... and applying it again says the box is in the symmetry already', refused(done, ['b'], 'already'))


def a_morph_shape_without_partners():
    # the points of a body all on one side of the plane: not one has a mirror partner
    places = [(20, 0, 0), (40, 0, 0), (20, 20, 0), (40, 20, 0), (20, 0, 20), (40, 0, 20), (20, 20, 20), (40, 20, 20)]
    for call in ('morph_polyhedron', 'morph_body'):
        script = symmetry_text('mirror') + ''.join(point_text('sh_p%d' % (k + 1), *p) for k, p in enumerate(places)) + 'sh = %s(\n    [%s],\n)\nsh\n' % (call, ', '.join('sh_p%d' % k for k in range(1, 9)))
        done = add(script, ['sh'])
        ns = make(done)
        check('%s with no mirror partner at all is mirrored whole when Shift+S is pressed on it' % call, 'sh_images = symmetry_images(sh, mirror, distinct=True)' in done and 'symmetry_image(' not in done.replace('symmetry_images(', ''))
        check('... and its image is on the other side of the plane', inside(ns['sh_images'], (-30, 10, 10)) and not inside(ns['sh'], (-30, 10, 10)) and inside(ns['sh'], (30, 10, 10)))


def a_spline_without_partners():
    # points that are models of their own: each gets an image, the spline goes through them (in reverse, a mirror image turns round)
    pts = [(10, 0, 0), (20, 10, 0), (30, 0, 0)]
    script = symmetry_text('mirror') + ''.join(point_text('c_p%d' % (k + 1), *p) for k, p in enumerate(pts)) + 'c = spline([c_p1, c_p2, c_p3])\nc\n'
    done = add(script, ['c'])
    ns = make(done)
    check('a spline with no mirror partner is mirrored: an image of each point, the spline through them', 'c_p1_m1 = symmetry_image(c_p1, mirror, 1)' in done and 'c_m1 = spline([c_p3_m1, c_p2_m1, c_p1_m1])' in done,
          done.split('\n')[-12:])
    xs = [v[0] for v in ns['c_m1'].vertices]
    check('... and the image lies on the other side of the plane, the spline itself where it was', max(xs) < 0 and min(v[0] for v in ns['c'].vertices) > 0, (min(xs), max(xs)))
    check('... and applying it again says it is in the symmetry already', refused(done, ['c'], 'already'))
    # a point on the plane is its own image and is used as it is
    on = [(0, 0, 0), (20, 10, 0), (30, 0, 0)]
    script = symmetry_text('mirror') + ''.join(point_text('c_p%d' % (k + 1), *p) for k, p in enumerate(on)) + 'c = spline([c_p1, c_p2, c_p3])\nc\n'
    done = add(script, ['c'])
    check('a point on the plane is its own image: no second point on top of it', 'c_p1_m1' not in done and 'c_m1 = spline([c_p3_m1, c_p2_m1, c_p1])' in done, done.split('\n')[-9:])
    # points written as three numbers are mirrored as numbers
    script = symmetry_text('mirror') + 'c = spline([(10, 0, 0), (20, 10, 0), (30, 0, 0)])\nc\n'
    done = add(script, ['c'])
    ns = make(done)
    check('a spline of three numbers is mirrored as numbers', 'c_m1 = spline([' in done and max(v[0] for v in ns['c_m1'].vertices) < 0, done.split('\n')[-3:])


def linked_points_stay_linked():
    places = [(-20, -14, -10), (20, -14, -10), (-20, 14, -10), (20, 14, -10), (-16, -9, 10), (16, -9, 10), (-16, 9, 10), (16, 9, 10), (0, 0, 19)]
    faces = '[(0, 2, 3, 1), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5), (4, 5, 8), (5, 7, 8), (7, 6, 8), (6, 4, 8)]'
    body = ''.join(point_text('sh_p%d' % (k + 1), *p) for k, p in enumerate(places)) + 'sh_faces = %s\nsh = morph_polyhedron(\n    [%s],\n)\nsh\n' % (faces, ', '.join('sh_p%d' % k for k in range(1, 10)))
    script = body + symmetry_text('mirror')
    done = add(script, ['sh'])
    check('a shape made of points with mirror partners, selected alone, is linked point by point (nothing is put on top of a point)', done.count('symmetry_image(') == 4 and 'sh_images' not in done, done.count('symmetry_image('))
    check('... and pressing it again says it is in the symmetry already, and adds no images', refused(done, ['sh'], 'already'))


def several_and_nothing():
    script = symmetry_text('mirror') + 'a = box((10, 10, 10), position=(25, 5, 5))\na\nb = sphere(4, position=(40, 20, 20))\nb\n'
    done = add(script, ['a', 'b'])
    ns = make(done)
    check('two models selected: each gets its images', 'a_images = symmetry_images(a, mirror, distinct=True)' in done and 'b_images = symmetry_images(b, mirror, distinct=True)' in done and 'a_images' in ns and 'b_images' in ns)
    later = 'a = box((10, 10, 10), position=(25, 5, 5))\na\nb = sphere(4, position=(40, 20, 20))\nb\n' + symmetry_text('mirror')
    done = add(later, ['a', 'b'])
    ns = make(done)
    check('... also when the symmetry is made after both (two lines at one place)', 'a_images' in ns and 'b_images' in ns and done.count('mirror = symmetry_planes(') == 1)
    material = symmetry_text('mirror') + "m = Material('x', 200000, 0.3, 7.8e-9, 250)\nm\n"
    check('a material is no shape: nothing to mirror, and the error says so (Shift+S is then the symmetry in use or not)', refused(material, ['m'], 'cannot be mirrored'))
    check('the symmetry itself cannot be mirrored', refused(material, ['mirror'], 'cannot be mirrored'))
    try:
        add('b = box((10, 10, 10), position=(25, 5, 5))\nb\n', ['b'])
        check('no symmetry in the script: an error that says so', False)
    except Exception as e:
        check('no symmetry in the script: an error that says so', 'symmetry' in str(e))


for name, fn in (('a box', a_box), ('morph shapes', a_morph_shape_without_partners), ('a spline', a_spline_without_partners), ('linked points', linked_points_stay_linked), ('several', several_and_nothing)):
    try:
        fn()
    except Exception:
        traceback.print_exc()
        bad.append(name + ' raised')
print('=' * 70)
print('ALL CHECKS PASSED' if not bad else '%d check(s) failed %s' % (len(bad), bad))
sys.exit(1 if bad else 0)
