'''
Closed-form surfaces: cheap primitives beyond planes, spheres and cylinders.

Each is a handful of arithmetic operations, so it meshes as fast as any
other shape.  The general forms are what the STEP importer fits to B-spline
faces (see import_step_parts); the named ones are for modeling:

    quadric(coefficients, center, scale)        any surface of degree 2
    extruded_curve(coefficients, direction, origin, scale)
                                                a cubic curve swept straight
    revolved_curve(coefficients, axis, origin, scale)
                                                a cubic curve spun around an axis
    helical_curve(coefficients, lead, axis, origin, offset, scale)
                                                a cubic curve screwed along an axis
    helical_sweep(profile, lead, center)        a cross-section screwed along z:
                                                helical gears, twisted flutes
    helical_revolve(profile, lead, center)      an axial profile screwed around z:
                                                threads, springs, auger flights
    coil_spring(radius, wire_radius, lead, turns, center)
    ellipsoid(radii, center)
    elliptic_cylinder(a, b, center)             along z
    paraboloid(k, center)                       z >= k (x^2 + y^2), opening up
    hyperboloid(a, c, center)                   one sheet, along z
    wave_block(size, amplitude, period, center) a block with a wavy top

Coefficient orders (in coordinates w = (p - origin) / scale):
    quadric:  x^2 y^2 z^2 xy xz yz x y z 1
    curves:   u^3 u^2v uv^2 v^3 u^2 uv v^2 u v 1
      extruded_curve: u, v across the direction (see _frame)
      revolved_curve: u = distance from the axis, v = along the axis
      helical_curve: u, v across the axis, turning with the height
All three return scale * f(w): inside where negative.
'''

import math

from fieldes.shape import Shape
from fieldes.stdlib.fieldargs import lowest

X, Y, Z = Shape.X, Shape.Y, Shape.Z


def _unit(v):
    n = math.sqrt(sum(c * c for c in v))
    if n == 0:
        raise ValueError('zero-length direction')
    return [c / n for c in v]


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def _frame(d):
    ''' Two unit vectors across d (the same choice the importer makes) '''
    d = _unit(d)
    e1 = _unit(_cross([1, 0, 0] if abs(d[0]) < 0.9 else [0, 1, 0], d))
    return e1, _cross(d, e1), d


def _local(origin, scale):
    return [(X() - origin[0]) / scale, (Y() - origin[1]) / scale, (Z() - origin[2]) / scale]


def _dot(w, e):
    return w[0] * e[0] + w[1] * e[1] + w[2] * e[2]


def _cubic(c, u, v):
    if len(c) != 10:
        raise ValueError('a cubic curve takes 10 coefficients')
    u2, v2 = u.square(), v.square()
    return (c[0] * u2 * u + c[1] * u2 * v + c[2] * u * v2 + c[3] * v2 * v +
            c[4] * u2 + c[5] * u * v + c[6] * v2 + c[7] * u + c[8] * v + c[9])


def quadric(coefficients, center=(0, 0, 0), scale=1):
    ''' Any surface of degree 2: ellipsoids, elliptic / parabolic /
        hyperbolic cylinders, cones, paraboloids, hyperboloids.
        coefficients: x^2 y^2 z^2 xy xz yz x y z 1, in w = (p - center) / scale
    '''
    c = list(coefficients)
    if len(c) != 10:
        raise ValueError('a quadric takes 10 coefficients')
    x, y, z = _local(center, scale)
    return scale * (c[0] * x.square() + c[1] * y.square() + c[2] * z.square() +
                    c[3] * x * y + c[4] * x * z + c[5] * y * z +
                    c[6] * x + c[7] * y + c[8] * z + c[9])


def extruded_curve(coefficients, direction=(0, 0, 1), origin=(0, 0, 0), scale=1):
    ''' A cubic curve g(u, v) = 0 in the plane across `direction`, swept
        straight along it (u, v: along the two axes _frame picks)
    '''
    e1, e2, _ = _frame(direction)
    w = _local(origin, scale)
    return scale * _cubic(list(coefficients), _dot(w, e1), _dot(w, e2))


def revolved_curve(coefficients, axis=(0, 0, 1), origin=(0, 0, 0), scale=1):
    ''' A cubic curve g(r, h) = 0 (r: distance from the axis, h: along it,
        from `origin`) spun around the axis
    '''
    e1, e2, a = _frame(axis)
    w = _local(origin, scale)
    # (the distance from the axis from the two axes across it: bounds well
    # over a box, unlike |w|^2 - h^2)
    r = (_dot(w, e1).square() + _dot(w, e2).square()).sqrt()
    return scale * _cubic(list(coefficients), r, _dot(w, a))


def ellipsoid(radii, center=(0, 0, 0)):
    ''' An ellipsoid with semi-axes radii = (a, b, c) along x, y, z, numbers or fields
        (distance-like near the surface; exact for a sphere)
    '''
    a, b, c = radii
    x, y, z = X() - center[0], Y() - center[1], Z() - center[2]
    k = lowest(a, b, c)
    return k * (((x / a).square() + (y / b).square() + (z / c).square()).sqrt() - 1)


def elliptic_cylinder(a, b, center=(0, 0, 0)):
    ''' An infinite cylinder along z with an elliptic section (semi-axes a, b) '''
    x, y = X() - center[0], Y() - center[1]
    return lowest(a, b) * (((x / a).square() + (y / b).square()).sqrt() - 1)


def paraboloid(k, center=(0, 0, 0)):
    ''' The inside of z = k (x^2 + y^2) (opening up from its vertex at center) '''
    x, y, z = X() - center[0], Y() - center[1], Z() - center[2]
    f = k * (x.square() + y.square()) - z
    grad = (4 * k * k * (x.square() + y.square()) + 1).sqrt()
    return f / grad


def hyperboloid(a, c, center=(0, 0, 0)):
    ''' One-sheet hyperboloid along z: (x^2 + y^2) / a^2 - z^2 / c^2 = 1,
        waist radius a (the inside is the part around the axis)
    '''
    x, y, z = X() - center[0], Y() - center[1], Z() - center[2]
    # distance from the axis minus the radius at that height
    return (x.square() + y.square()).sqrt() - a * (1 + z.square() / (c * c)).sqrt()


def wave_block(size, amplitude, period, center=(0, 0, 0)):
    ''' A block (size = (sx, sy, sz)) whose top is a wave:
        z_top = sz / 2 + amplitude sin(2 pi x / period) sin(2 pi y / period)
    '''
    sx, sy, sz = size
    x, y, z = X() - center[0], Y() - center[1], Z() - center[2]
    k = 2 * math.pi / period
    top = z - sz / 2 - amplitude * (k * x).sin() * (k * y).sin()
    sides = ((x.abs() - sx / 2).max(y.abs() - sy / 2)).max(-z - sz / 2)
    return top.max(sides)


def helical_curve(coefficients, lead, axis=(0, 0, 1), origin=(0, 0, 0), offset=(0, 0), scale=1):
    ''' A cubic curve g(u, v) = 0 in the plane across the axis (through
        `origin`), turning as it advances along the axis -- one full turn per
        `lead` (a negative lead turns the other way): the flank of a helical
        gear or a worm, a thread, a coil.  The form the STEP importer fits to
        such faces; u, v are measured from `offset` in the turned plane.
    '''
    e1, e2, a = _frame(axis)
    w = _local(origin, scale)
    phi = _dot(w, a) * (2 * math.pi * scale / lead)
    cs, sn = phi.cos(), phi.sin()
    u, v = _dot(w, e1), _dot(w, e2)
    return scale * _cubic(list(coefficients), u * cs + v * sn - offset[0], v * cs - u * sn - offset[1])


def helical_sweep(profile, lead, center=(0, 0, 0)):
    ''' `profile` (a shape; its cross-section in the xy plane is what counts)
        screwed along the z axis through `center`: turned by one full turn
        per `lead` of height (negative: the other way).  A gear outline gives
        a helical gear, a drill's outline its twisted flutes (for threads and
        springs, whose profile is drawn through the axis: helical_revolve).
        Unbounded along z -- cut it to length, e.g. with extrude_z.  Kept
        close to a distance by dividing out the twist's stretching.
    '''
    k = 2 * math.pi / lead
    x, y, z = X() - center[0], Y() - center[1], Z() - center[2]
    phi = k * z
    cs, sn = phi.cos(), phi.sin()
    turned = Shape.wrap(profile).remap(x * cs + y * sn + center[0], y * cs - x * sn + center[1], Z())
    stretch = (1 + (k * k) * (x.square() + y.square())).sqrt()
    return turned / stretch


def helical_revolve(profile, lead, center=(0, 0, 0)):
    """ Like revolving a profile around the z axis (through `center`), but
        advancing `lead` along it per turn (negative: left-handed): a
        thread, a coil spring, an auger's flight.  The profile is a 2D shape
        in the xy plane: x = distance from the axis, y = height; it must fit
        within one lead of height (|y| < lead / 2), since the result repeats
        every lead.  Unbounded along z -- cut it to length.
    """
    L = abs(lead)
    x, y, z = X() - center[0], Y() - center[1], Z() - center[2]
    r = (x.square() + y.square()).sqrt()
    turn = y.atan2(x) * (lead / (2 * math.pi))           # the height this angle has risen
    h = (z - turn + L / 2) % L - L / 2                   # height within the current turn
    return Shape.wrap(profile).remap(r, h, Z() * 0)


def coil_spring(radius, wire_radius, lead, turns, center=(0, 0, 0)):
    """ A coil spring along z: `turns` turns of round wire (radius
        `wire_radius`) around a coil of radius `radius`, rising `lead` per
        turn, standing on `center`, its ends cut flat.
    """
    u, v = X() - radius, Y()
    wire = (u.square() + v.square()).sqrt() - wire_radius
    coil = helical_revolve(wire, lead, center)
    z = Z() - center[2]
    return coil.max(-z).max(z - turns * abs(lead))
