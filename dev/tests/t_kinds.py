# The kinds of models (fieldes.kinds): what each value is, how a 2D shape / a surface / a point / a field is drawn
from fieldes import *
from fieldes.kinds import kind_of, KINDS

failures = []


def expect(label, got, want):
    ok = got == want
    print('%-48s %-12s %s' % (label, str(got), 'ok' if ok else 'FAIL (wanted %s)' % (want,)))
    if not ok:
        failures.append(label)


expect('sphere(5)', kind_of(sphere(5)), 'solid')
expect('box_exact(...)', kind_of(box_exact((0, 0, 0), (1, 1, 1))), 'solid')
expect('difference(sphere, box)', kind_of(difference(sphere(5), box_exact((0, 0, 0), (1, 1, 1)))), 'solid')
expect('move(sphere)', kind_of(move(sphere(5), (1, 0, 0))), 'solid')
expect('ellipsoid (a python-made primitive)', kind_of(ellipsoid((5, 4, 3))), 'solid')
expect('circle(3)  (2D)', kind_of(circle(3)), 'profile')
expect('rectangle(...)  (2D)', kind_of(rectangle((0, 0), (3, 2))), 'profile')
expect('move(circle) (2D moved)', kind_of(move(circle(3), (1, 1, 0))), 'profile')
expect('union(circle, circle)', kind_of(union(circle(3), circle(2, (4, 0)))), 'profile')
expect('extrude_z(circle) (3D)', kind_of(extrude_z(circle(3), 0, 5)), 'solid')
expect('x_field()', kind_of(x_field()), 'field')
expect('x_field() * 2 + 1', kind_of(x_field() * 2 + 1), 'field')
expect('depth_below(sphere)', kind_of(depth_below(sphere(5))), 'field')
expect('ramp(z_field(), (0, 10), (1, 2))', kind_of(ramp(z_field(), (0, 10), (1, 2))), 'field')
expect('noise_field(4)', kind_of(noise_field(4)), 'field')
expect('sphere - 0.3 * noise_field(4) (body - field)', kind_of(sphere(5) - 0.3 * noise_field(4)), 'solid')
expect('thicken(plane) (a wall round a sheet)', kind_of(thicken(plane((0, 0, 0), (0, 0, 1)), 1.0)), 'solid')
expect('point(1, 2, 3)', kind_of(point(1, 2, 3)), 'point')
expect('plane(...)', kind_of(plane((0, 0, 1), (0, 0, 1))), 'surface')
expect('sphere_surface(5)', kind_of(sphere_surface(5)), 'surface')
expect('cylinder_surface(3)', kind_of(cylinder_surface(3)), 'surface')
expect('wave_surface(2, 10)', kind_of(wave_surface(2, 10)), 'surface')
expect('cell_periodic("gyroid")', kind_of(cell_periodic('gyroid')), 'cell')
expect('cylindrical(cells_around=8)', kind_of(cylindrical(cells_around=8)), 'cell')
expect('steel (a material)', kind_of(steel), 'conditions')
expect('fixed(box)', kind_of(fixed(box_exact((0, 0, 0), (1, 1, 1)))), 'conditions')
expect('force(box, ...)', kind_of(force(box_exact((0, 0, 0), (1, 1, 1)), (0, 0, -1))), 'conditions')
expect('5 (a number)', kind_of(5), None)
expect('"text"', kind_of('text'), None)

# how each is drawn
pt = point(3, 4, 5)
expect('point is drawn as its own small ball', pt._display() is pt, True)
ball = pt._display()
vals = evaluate(ball, [(3, 4, 5), (3, 4, 9), (30, 4, 5)])
expect('  ball: inside at its centre, outside away', (vals[0] < 0, vals[1] > 0, vals[2] > 0), (True, True, True))
disc = circle(3)._display()
vals = evaluate(disc, [(0, 0, 0), (0, 0, 2), (4, 0, 0), (1, 1, 0.5)])
expect('2D shape is drawn flat', (vals[0] < 0, vals[1] > 0, vals[2] > 0, vals[3] > 0), (True, True, True, True))
sheet = plane((0, 0, 0), (0, 0, 1))._display()
vals = evaluate(sheet, [(1, 1, 0), (1, 1, 3), (1, 1, -3)])
expect('surface is drawn as a thin sheet', (vals[0] < 0, vals[1] > 0, vals[2] > 0), (True, True, True))
shown = depth_below(sphere(5))._display()
expect('a field draws nothing in the viewport (the section viewer shows it): empty everywhere', all(v > 0 for v in evaluate(shown, [(0, 0, 0), (100, 0, 0), (0, 0, -3)])), True)
body = sphere(5)
expect('a 3D shape is drawn as itself', body._display() is body, True)

# a point is a position
d = distance_to_point(point(3, 4, 0))
expect('distance_to_point(point)', round(evaluate(d, (0, 0, 0)), 4), 5.0)
a = attractor(point(0, 0, 0), 10)
expect('attractor(point, ...)', round(evaluate(a, (0, 0, 0)), 4), 1.0)

print('FAILED: ' + ', '.join(failures) if failures else 'ALL OK')

# a model keeps its kind through the lines the model tree writes under it (handles, expose, lock)
pt = point(3, 4, 5)
moved = handles(pt, move=(1, 0, 0))
expect('handles(point) is still a point', kind_of(moved), 'point')
expect('  and its coordinates follow the move', (moved.xyz[0], moved.xyz[1], moved.xyz[2]), (4.0, 4.0, 5.0))
expect('lock(point) is still a point', kind_of(lock(pt)), 'point')
sheet = plane((0, 0, 1), (0, 0, 1))
expect('handles(surface) is still a surface', kind_of(handles(sheet, move=(0, 0, 1))), 'surface')
expect('lock(surface) is still a surface', kind_of(lock(sheet)), 'surface')
f = depth_below(sphere(5))
expect('lock(field) is still a field', kind_of(lock(f)), 'field')
c = circle(3)
expect('handles(2D shape) is still 2D', kind_of(handles(c, move=(1, 0, 0))), 'profile')
expect('handles with the rotations of the gizmo: still 2D',
       kind_of(handles(c, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))), 'profile')
exposed = expose(c, [var(0.0), var(0.0), var(3.0)])
expect('expose(2D shape) is still 2D', kind_of(exposed), 'profile')
expect('and with the gizmo on it, and locked', kind_of(lock(handles(exposed, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0))))), 'profile')
print('FAILED: ' + ', '.join(failures) if failures else 'ALL OK')
