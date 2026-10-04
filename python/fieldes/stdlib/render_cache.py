'''
The render cache: keep what FielDes meshes, so a shape that has not changed is on screen at once.

    part = difference(box_exact((0, 0, 0), (40, 30, 10)), sphere(8))
    part                            # its mesh is kept: nothing to write
    part = render_cache(part, False)    # ... unless you opt out (the model tree's cache button writes this line)

Meshing is the slow part of showing a shape.  Every shape's finished mesh is saved (in FielDes's cache
folder, outside your project) once it has taken a while to make, and read back when the same shape is
shown again -- the next time you open the script, or after you changed the script and changed it back --
instead of being meshed again.  When anything about the math changes -- an operation, a number, a
var() you dragged, an imported file, the render region, the resolution -- the shape is not the same
one any more: it is meshed again, shown as usual, and the new mesh is kept.

It is ON unless you turn it off: click the cache button on a row of the model tree (or write
`part = render_cache(part, False)`) to turn it off for that shape; click again (or delete the line) for
the default.  `part = render_cache(part)` says the default aloud and keeps the mesh however quickly it was
made.  The line is the whole setting: it is saved with the script and nothing else about the script
changes.  The shape itself is not touched.

A shape is kept when everything it is made of can be recognised from one run to the next: nodes,
numbers, var()s, imported meshes and parts, lattices, fields.  A shape that is shown with the fields of
an analysis (a result coloured by stress) is not -- the analysis has caches of its own.

The kept meshes are deleted oldest first when they fill more than 4 GB (the environment variable
FIELDES_RENDER_CACHE_MB sets that); Settings > Clear the caches deletes them all (and the files of the field cache).

Every field a script builds with `name = <expression>` is kept the same way -- by the statement's text, the exact
content of what it reads, the var() numbers and the code that builds it -- so a lattice laid out on a part is not laid
out again when the script is run again, also after FielDes was closed (when every part of the field can be saved).
FIELDES_FIELD_CACHE_DIR moves the files; the details are in caching-and-performance.md.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
from fieldes.ffi import lib
from fieldes.shape import Shape

__all__ = ['render_cache', 'render_cache_key']


def render_cache(shape, on=True):
    ''' `shape`, with its finished mesh kept on disk: shown from there when the same shape is rendered
        again (see the module).  Every shape is kept by default; `render_cache(shape, False)` opts it out, and
        `render_cache(shape)` keeps it however quickly it was meshed.  Returns the shape itself in every
        other way: it keeps its bounds, handles, colours and exact regions.

        Write it as the last line of a shape's definition, after handles() and expose():

            part = render_cache(part)

        (FielDes's model tree has a button for it on every shape.) '''
    if not isinstance(shape, Shape):
        raise TypeError('render_cache(shape): shape must be a Shape, not {}'.format(type(shape).__name__))
    ptr = lib.libfive_tree_copy(shape.ptr)
    try:
        out = type(shape)(ptr)
    except TypeError:
        out = Shape(ptr)
    for name, value in shape.__dict__.items():
        if name != 'ptr':
            out.__dict__[name] = value
    out._render_cache = bool(on)
    return out


def render_cache_key(shape):
    ''' The key the render cache keeps `shape`'s mesh by, as text -- the same in every run of the program for the
        same math, another one when anything about the math changes (an operation, a number, a var() with the
        value it has now, an imported file) -- or None when the shape cannot be kept: it depends on something no
        other run could recognise (a solved analysis).  (The region, resolution and quality it is meshed at are
        part of the key the application uses, which is made of this and them.) '''
    import ctypes
    from fieldes.stdlib.fields import _script_vars
    if not isinstance(shape, Shape):
        raise TypeError('render_cache_key(shape): shape must be a Shape')
    fn = getattr(lib, 'libfive_tree_persistent_key', None)
    if fn is None:
        return None
    known = _script_vars()
    if known is not None:
        ptr = fn(shape.ptr, known[0], known[1], known[2])
    else:
        ptr = fn(shape.ptr, None, None, 0)
    if not ptr:
        return None
    try:
        return ctypes.c_char_p(ptr).value.decode('ascii')
    finally:
        lib.libfive_free_str(ctypes.cast(ptr, ctypes.c_char_p))
