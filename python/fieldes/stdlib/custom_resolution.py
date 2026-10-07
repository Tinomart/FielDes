'''
A resolution of its own for one body.

The viewport meshes the whole scene at one resolution (view.set_resolution): fine enough for the finest thing in it, which is
more than the rest needs.  `custom_resolution(body, resolution)` gives ONE body a resolution of its own, in samples per mm,
whatever the scene's is: a part that is intricate is drawn fine and the rest of the scene stays coarse and quick -- or a part
that is big and plain is drawn coarser than the scene.

    from fieldes import *

    view.set_resolution(2)                        # the scene: 2 samples per mm
    gear = custom_resolution(gear, 8)             # ... except the gear, drawn at 8
    gear                                          # (it is shown by the line that names it, as always)

The body is meshed on its own, over a cube round it, at that resolution; the number is the resolution, not a scale of the scene's,
so it stays what it is when the scene's resolution is changed.  Nothing is drawn outside the region of the scene (view.set_bounds), as
for every shape: a body that reaches out of it is drawn at its own resolution inside it.  It changes how the body is DRAWN, not what it
is: an analysis, a boolean or an export of it is the same.

The finest a body can be drawn is 2000 samples along its longest side (a longer one would be hundreds of millions of cells): a
resolution above that is not an error, the body is drawn at the finest there is, and the model tree's row says what is used
(`resolution 8 -> 7.6`).

In FielDes, right-click a body and choose "Custom resolution": the line is written under its definition, and the number is a field
under the body in the model tree (type in it; the bin takes the line away).
'''
import math
import time

from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['custom_resolution']

# the most samples across the longest side of a body: a finer one is a mesh of hundreds of millions of cells
MAX_SAMPLES = 2000


def _box_of(shape, bounds):
    if bounds is not None:
        lo, hi = bounds
        return tuple(float(v) for v in lo), tuple(float(v) for v in hi)
    b = getattr(shape, '_bounds', None)
    if not b:
        try:
            from fieldes.app_support import _estimate_bounds
            b = _estimate_bounds([shape], time.time() + 2.0)
        except Exception:                                       # noqa
            b = None
    if not b:
        raise ValueError('custom_resolution: the extent of the shape could not be found: give bounds=((x0, y0, z0), (x1, y1, z1))')
    return tuple(float(v) for v in b[0]), tuple(float(v) for v in b[1])


def custom_resolution(shape, resolution, bounds=None):
    ''' `shape` drawn at a resolution of its own: `resolution` samples per mm, whatever the scene's resolution is (see the module).

        shape       a body
        resolution  samples per mm (a number above 0); 2 draws features of half a millimetre, 10 of a tenth
        bounds      ((x0, y0, z0), (x1, y1, z1)) of the body, if its extent cannot be found

        Returns the body itself in every other way; only the way it is meshed for the viewport is changed. '''
    if not isinstance(shape, Shape):
        raise TypeError('custom_resolution(shape, resolution): shape must be a Shape, not {}'.format(type(shape).__name__))
    try:
        r = float(resolution)
    except (TypeError, ValueError):
        raise ValueError('custom_resolution: the resolution is a number (samples per mm)')
    if not (r > 0 and math.isfinite(r)):
        raise ValueError('custom_resolution: the resolution must be more than 0 (samples per mm)')
    lo, hi = _box_of(shape, bounds)
    extent = max(hi[i] - lo[i] for i in range(3))
    if not extent > 0:
        raise ValueError('custom_resolution: the box of the shape has no size')
    # A resolution that is too fine for the body is not an error: it is the finest there is, and the number says what it was asked
    # (an error would take the whole picture away while a number is being typed: "1" is fine, "10" is too much, and the row to correct
    # it is gone).  The model tree's row shows what is used
    used = r
    samples = r * extent
    if samples > MAX_SAMPLES:
        used = MAX_SAMPLES / extent
        samples = float(MAX_SAMPLES)
    # The cube the body is meshed in has 2**d cells a side, each one resolution long: the nearest cube that holds the body
    d = max(1, int(math.ceil(math.log2(samples * 1.05))))
    side = 2.0 ** d / used
    ptr = lib.libfive_tree_copy(shape.ptr)
    try:
        out = type(shape)(ptr)
    except TypeError:
        out = Shape(ptr)
    for name, value in shape.__dict__.items():
        if name != 'ptr':
            out.__dict__[name] = value
    # (the box, the resolution just under the cube's so that it is exactly `d` levels deep, the side of the cube, and a scene resolution
    # of -1: the resolution is absolute, it does not follow the scene's)
    out._render_hint = (lo, hi, 0.999 * used, side, -1.0)
    out._custom_resolution = r
    out._custom_resolution_used = used              # (below r when the body is too long to be meshed that finely)
    return out
