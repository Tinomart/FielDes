'''
A resolution of its own for one body.

The viewport meshes the scene at one resolution (view.set_resolution): fine enough for the finest thing in it, which is
more than the rest needs.  `custom_resolution(body, resolution)` gives ONE body the resolution you say -- the number the scene's resolution
is, for this body alone: a part that is intricate is drawn finer and the rest of the scene stays coarse and quick, or a part that is big
and plain is drawn coarser than the scene.

    from fieldes import *

    view.set_resolution(2)                        # the scene: resolution 2
    gear = custom_resolution(gear, 8)             # ... except the gear, drawn as if the scene's were 8
    gear                                          # (it is shown by the line that names it, as always)

It is the SAME number as the scene's, so it is read the same way: `custom_resolution(gear, 2)` in a scene at 2 is the gear as it was, and
twice as much is twice as fine.  And it scales as the scene's does: with auto scale on (the default) a small body in a big region is
drawn finer than the number says, by how small it is, whatever it is given -- so there is no need to write a number far from the scene's
to get a picture as fine as the scene's own.  The body is meshed over the region of the scene (view.set_bounds), as every shape is.  It
changes how the body is DRAWN, not what it is: an analysis, a boolean or an export of it is the same.

The finest a body can be drawn is 2000 samples along its longest side (a longer one would be hundreds of millions of cells): a resolution
above that is not an error, the body is drawn at the finest there is.

In FielDes, right-click a body and choose "Custom resolution": the line is written under its definition, and its number is a row under
the body in the model tree like any other argument -- click it and the number is selected in the code, ready to be typed over (the bin
takes the line away).
'''
import math

from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['custom_resolution']


def custom_resolution(shape, resolution):
    ''' `shape` drawn at a resolution of its own: `resolution` is the number the scene's resolution is (see the module), for this body alone.

        shape       a body
        resolution  the resolution (a number above 0), as view.set_resolution says it: the scene's own number gives the body as it was drawn,
                    twice as much draws it twice as fine

        Returns the body itself in every other way; only the way it is meshed for the viewport is changed. '''
    if not isinstance(shape, Shape):
        raise TypeError('custom_resolution(shape, resolution): shape must be a Shape, not {}'.format(type(shape).__name__))
    try:
        r = float(resolution)
    except (TypeError, ValueError):
        raise ValueError('custom_resolution: the resolution is a number, as the scene\'s is (view.set_resolution)')
    if not (r > 0 and math.isfinite(r)):
        raise ValueError('custom_resolution: the resolution must be more than 0')
    ptr = lib.libfive_tree_copy(shape.ptr)
    try:
        out = type(shape)(ptr)
    except TypeError:
        out = Shape(ptr)
    for name, value in shape.__dict__.items():
        if name != 'ptr':
            out.__dict__[name] = value
    # (what the viewport reads: the number this body is meshed at, in the place of the scene's)
    out._resolution_override = r
    out._custom_resolution = r
    return out
