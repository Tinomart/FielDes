# exclude(): two fields, always united -- the free one that operations reshape and the locked one they never touch.
# Headless: field values (no mesh is involved), the refusals, the STEP exact surface as a field, and a check that every
# function of the library is classified (fieldes.stdlib.excluded) so that a new one cannot silently drop a lock.
import inspect
import os
import sys

from fieldes import *
from fieldes.ffi import lib
from fieldes.stdlib import excluded as ex
from fieldes.stdlib import transforms

failed = 0


def check(name, ok, detail=''):
    global failed
    print('  %-62s %s %s' % (name, 'ok' if ok else 'FAILED', detail))
    if not ok:
        failed += 1


def inside(shape, p):
    return shape(*p) < 0


def raises(fn, kind):
    try:
        fn()
    except kind as e:
        return True, str(e)[:80]
    except Exception as e:
        return False, 'wrong error: %r' % (e,)
    return False, 'no error'


print('== every function of the library is classified')
listed = set()
for table in (ex.KEEPS_ALL, ex.KEEPS_FIRST, ex.DECORATES, ex.MOVES, ex.REFUSES, ex.THROUGH, ex.NO_BODY):
    for module, names in table.items():
        for n in names:
            listed.add((module, n))
            mod = sys.modules.get('fieldes.stdlib.' + module) or __import__('fieldes.stdlib.' + module, fromlist=['x'])
            if not hasattr(mod, n):
                check('%s.%s named in a table exists' % (module, n), False)
missing = []
for module in ['csg', 'shapes', 'text', 'transforms', 'handles', 'render_cache', 'cad_import', 'tessellated_import',
               'mesh_import', 'fea', 'boundary_conditions', 'selection', 'thermal', 'fluid', 'fields', 'regression',
               'surfaces', 'lattices', 'conformal', 'excluded']:
    mod = sys.modules['fieldes.stdlib.' + module]
    for n, f in vars(mod).items():
        if n.startswith('_') or not inspect.isfunction(f) or getattr(f, '__module__', None) not in (mod.__name__, None):
            continue
        if module in ex.NO_BODY_MODULES or (module, n) in listed:
            continue
        if getattr(f, '__wrapped__', None) is not None:
            continue
        # (the wrapped copies made here carry the module of the guard)
        if f.__module__ == 'fieldes.stdlib.excluded' and (module, n) in listed:
            continue
        if f.__module__ == mod.__name__:
            missing.append('%s.%s' % (module, n))
check('no function is left out of the tables', not missing, ', '.join(missing))

print('== operations on an excluded box (plain shapes: the locked field is the shape itself)')
part = box_exact((0, 0, 0), (20, 20, 20))
region = box_exact((10, -1, -1), (21, 21, 21))          # the right half, and a hair of its end
e = exclude(part, region)
check('same shape as before', inside(e, (5, 10, 10)) and inside(e, (15, 10, 10)) and not inside(e, (25, 10, 10)))
check('it carries one lock', len(ex.locks_of(e)) == 1)

o = offset(e, 2)
check('offset: grows where it is free (x = -1.5)', inside(o, (-1.5, 10, 10)))
check('offset: does not grow inside the region (x = 20.5)', not inside(o, (20.5, 10, 10)))
check('offset: free again beyond the region (x = 21.5)', inside(o, (21.5, 10, 10)))
check('offset keeps the lock for the next operation', len(ex.locks_of(o)) == 1)
oo = shell(o, 1)
check('shell of that: still no wall growing into the region', not inside(oo, (20.5, 10, 10)))
check('method form (Shape.offset) is guarded too', not inside(e.offset(2), (20.5, 10, 10)))

ball = sphere(4, (15, 10, 22))                            # sits over the region's top, partly above it
u = union(e, ball)
check('union: the ball is not added inside the region', not inside(u, (15, 10, 20.5)))
check('union: the ball is added outside it (z = 24)', inside(u, (15, 10, 24)))
check('union(ball, e) is guarded too (the locked one may be any argument)', not inside(union(ball, e), (15, 10, 20.5)))

cut = difference(e, sphere(5, (15, 10, 10)))
check('difference: the cut does not reach into the region', inside(cut, (15, 10, 10)))
cut2 = difference(e, sphere(5, (5, 10, 10)))
check('difference: the cut works where it is free', not inside(cut2, (5, 10, 10)))
tool = difference(box_exact((-5, -5, -5), (30, 30, 30)), e)
check('an excluded shape as the TOOL of a difference is not added back', not inside(tool, (15, 10, 10)))

print('== moving carries both fields along')
m = move(e, (5, 0, 0))
check('moved: the box is there', inside(m, (7, 10, 10)) and inside(m, (24, 10, 10)) and not inside(m, (26, 10, 10)))
mo = offset(m, 2)
check('moved, then offset: locked end did not grow (x = 25.5)', not inside(mo, (25.5, 10, 10)))
check('moved, then offset: region moved with it (x = 26.5 grew)', inside(mo, (26.5, 10, 10)))
r = rotate_z(e, 1.5707963267948966, (10, 10, 0))
check('rotated a quarter turn about the middle: still a box', inside(r, (10, 5, 10)) and inside(r, (10, 15, 10)))
for fn_name, fn in (('scale_xyz', lambda s: scale_xyz(s, (1, 2, 1))), ('reflect_x', lambda s: reflect_x(s)),
                    ('handles', lambda s: handles(s, move=(3, 0, 0)))):
    out = fn(e)
    check('%s keeps the exclusion' % fn_name, len(ex.locks_of(out)) == 1)
h = handles(e, move=(var(0), var(0), var(0)))
check('handles() on an excluded shape', len(ex.locks_of(h)) == 1 and inside(h, (5, 10, 10)))

print('== what would separate the two fields raises')
for name, call in (('array_x', lambda: array_x(e, 3, 30)), ('symmetric_x', lambda: symmetric_x(e)),
                   ('mirror_x', lambda: mirror_x(e, 0)), ('repeat', lambda: repeat(e, (50, 50, 50))),
                   ('twist_z', lambda: twist_z(e, 1.0)), ('bend_z', lambda: bend_z(e, 100)),
                   ('taper_x_y', lambda: taper_x_y(e, (0, 0, 0), 20, 0.5)),
                   ('attract', lambda: attract(e, (0, 0, 0), 10)),
                   ('expose', lambda: expose(e, [0.0]))):
    ok, detail = raises(call, ex.ExcludedError)
    check('%s refuses' % name, ok, detail)
check('...and works before exclude()', isinstance(array_x(part, 3, 30), Shape))
check('...so exclude(array_x(part, 3, 30), region) is the way',
      inside(exclude(array_x(part, 3, 30), region), (35, 10, 10)))

print('== several regions, several exclusions, decorations')
two = exclude(part, box_exact((-1, -1, -1), (6, 21, 21)), sphere(4, (15, 10, 10)))
check('two regions are one (union)', len(ex.locks_of(two)) == 1)
o2 = offset(two, 2)
check('offset: grows between them (x = 8, z = 22)', inside(o2, (8, 10, 21.5)))
check('offset: locked in the first region (x = 3, z = 20.5)', not inside(o2, (3, 10, 20.5)))
twice = exclude(exclude(part, box_exact((-1, -1, -1), (6, 21, 21))), box_exact((14, -1, -1), (21, 21, 21)))
check('excluded twice: two locks', len(ex.locks_of(twice)) == 2)
o3 = offset(twice, 2)
check('twice: locked at both ends, free in the middle', not inside(o3, (3, 10, 20.5)) and not inside(o3, (17, 10, 20.5))
      and inside(o3, (10, 10, 21.5)))
f = colored(e, x_field())
check('colored() keeps the exclusion', len(ex.locks_of(f)) == 1)
check('the plain shape is untouched by all this', not inside(part, (25, 10, 10)) and not hasattr(part, '_locks'))
ok, detail = raises(lambda: exclude(part), ValueError)
check('exclude() with nothing to lock says so', ok, detail)
lst = [(part, ((0, 0, 0), (20, 20, 20)))]
check('a list that is not an import goes through as it is', isinstance(exclude(lst, region), list))

print('== a part imported from a STEP file: the locked field is the exact surface, meshed from the file')
here = os.path.dirname(os.path.abspath(__file__))
step = os.path.normpath(os.path.join(here, '..', '..', 'examples', 'step', 'PivotBearingSupportBracket.STEP'))
parts = import_step_parts(step)
p0, (lo, hi) = parts[0]
size = max(hi[i] - lo[i] for i in range(3))
mid = tuple(0.5 * (lo[i] + hi[i]) for i in range(3))
reg = box_exact(tuple(mid[i] - 0.25 * size for i in range(3)), tuple(mid[i] + 0.25 * size for i in range(3)))
xp = exclude(p0, reg)
check('exclude on an import part: one lock, with the part as source',
      len(ex.locks_of(xp)) == 1 and len(xp._exact_sources) == 1)
# the locked field is the exact surface: its mesh points are where the field is zero (inside the region)
from fieldes.stdlib import cad_import
src = xp._exact_sources[0]
locked_field = ex.locks_of(xp)[0][0]
import ctypes
mat = (ctypes.c_double * 16)(*[float(v) for row in src.scale for v in row])
mesh = lib.libfive_step_exact_surface(src.path.encode('utf-8'), src.solid, src.instance, mat, 64)
mc = mesh.contents
pts = [(mc.verts[i].x, mc.verts[i].y, mc.verts[i].z) for i in range(0, mc.vert_count, max(1, mc.vert_count // 400))]
lib.libfive_mesh_delete(mesh)
inreg = [q for q in pts if reg(*q) < -0.02 * size]
vals = [abs(xp(*q)) for q in inreg]
check('%d exact-surface points inside the region are on the exclusion\'s surface' % len(inreg),
      len(inreg) > 5 and max(vals) < 1e-3 * size, 'max |value| %.2g' % (max(vals) if vals else -1))
# and outside the region the shape is still the import's own field
out_pts = [q for q in pts if reg(*q) > 0.05 * size][:50]
same = all(abs(xp(*q) - p0(*q)) < 1e-4 * size for q in out_pts)
check('outside the region it is the import\'s field', same and len(out_pts) > 5)
# an operation on it keeps the exact surface inside the region
o4 = offset(xp, 0.5)
q = inreg[0]
check('offset: inside the region the surface did not move', abs(o4(*q)) < 1e-3 * size, 'value %.3g' % o4(*q))
mv = move(xp, (7, 0, 0))
q2 = (q[0] + 7, q[1], q[2])
check('moved with handles-style move: the exact surface moved along', abs(mv(*q2)) < 1e-3 * size, 'value %.3g' % mv(*q2))
mv2 = handles(xp, move=(var(0), var(0), var(0)))
check('the same through handles()', abs(mv2(*q)) < 1e-3 * size)
mpart = move(p0, (7, 0, 0))                          # moved BEFORE the exclusion: the source remembers it
xm = exclude(mpart, move(reg, (7, 0, 0)))
check('part moved first, then excluded', abs(xm(*q2)) < 1e-3 * size, 'value %.3g' % xm(*q2))
print('  (a second exclude of the same part reuses the field: %d surface(s) kept)' % len(cad_import._surface_fields))

print('failed:', failed)
sys.exit(1 if failed else 0)
