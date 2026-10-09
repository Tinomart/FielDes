'''
Measuring a body: its box and its middle.

    box = bounding_box(part)          # the smallest box, aligned with the axes, that holds the part: a body of its own
    middle = center(part)             # the middle of that box: a point

Both are what a script needs all the time -- to put something in the middle of a part, to give a region to a condition, to size
a cell to a part, to place the next part against this one -- and both are in the right-click menu of a body.  They are made when
the script runs, from the extent the body has then (a body that is dragged, or has var() numbers, is measured again by the run it
causes), and the extent is the one the body knows exactly -- a box, an imported part -- or else the one found by searching its
field, which can be a little roomy for a body with soft edges.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
from fieldes.shape import Shape
from fieldes.stdlib.fields import _shape_bounds
from fieldes.stdlib.points import point
from fieldes.stdlib.shapes import box_exact

__all__ = ['bounding_box', 'center']


def _snap(value, tolerance):
    ''' The number with the fewest decimals that is within `tolerance` of `value`: the extent of a shape is found by a search that is right to a
        thousandth of its size, and a box that is 60 long should say 60, not 60.018 '''
    for digits in range(0, 8):
        r = round(value, digits)
        if abs(r - value) <= tolerance:
            return float(r) if r != 0 else 0.0
    return float(value)


def _extent(body, what):
    ''' ((x0, y0, z0), (x1, y1, z1)) of a body, or an error that says what could not be measured '''
    if not isinstance(body, Shape):
        raise TypeError('%s needs a body (a model of the script), not %r' % (what, type(body).__name__))
    try:
        lo, hi = _shape_bounds(body, None)
    except ValueError:
        raise ValueError('%s: the extent of this body cannot be found (it is empty, or open on a side): '
                         'give it a closed shape to measure' % what) from None
    if getattr(body, '_bounds', None):
        return tuple(lo), tuple(hi)              # (known exactly: a part that was imported, a box this function made)
    size = max(float(hi[i]) - float(lo[i]) for i in range(3)) or 1.0
    magnitude = max([1.0] + [abs(float(c)) for c in list(lo) + list(hi)])
    tolerance = max(2e-3 * size, 3e-6 * magnitude)     # (what the search is right to: a thousandth of the size, see bounds_impl in the kernel)
    return (tuple(_snap(float(c), tolerance) for c in lo), tuple(_snap(float(c), tolerance) for c in hi))


def bounding_box(body):
    ''' The smallest box, aligned with the axes, that holds `body`: a body of its own (`box_exact` between the body's lowest and highest
        corner).  Use it as a region, as the space to lay something out in, or to read the size of a part:

            box = bounding_box(part)
            base = fixed(box)

        It is measured when the script runs, from the extent the body knows exactly (a box, an imported part) or the one found by
        searching its field. '''
    lo, hi = _extent(body, 'bounding_box')
    out = box_exact(tuple(float(c) for c in lo), tuple(float(c) for c in hi))
    out._bounds = (tuple(float(c) for c in lo), tuple(float(c) for c in hi))
    return out


def center(body):
    ''' The middle of `body`'s bounding box, as a point:

            middle = center(part)
            part = move(part, (-middle.x, -middle.y, -middle.z))        # the part about the origin

        It is measured when the script runs, like bounding_box. '''
    lo, hi = _extent(body, 'center')
    return point(*[0.5 * (float(lo[i]) + float(hi[i])) for i in range(3)])
