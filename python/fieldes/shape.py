'''
Python library of FielDes, built on the libfive CAD kernel
Copyright (C) 2021  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''

import ctypes
import numbers
import tempfile
import subprocess

from fieldes.ffi import (lib, libfive_region_t, libfive_interval_t,
                         libfive_vec3_t, libfive_tree)

def _combined_kind(shapes):
    ''' The kind of what is made of these shapes by arithmetic: a number is a number ('const'), fields combine into a
        field, and anything else -- a body, or a shape of no known kind -- makes a body (None) '''
    kinds = [s._kind for s in shapes]
    if all(k == 'const' for k in kinds):
        return 'const'
    if all(k in ('field', 'const') for k in kinds):
        return 'field'
    return None


def _wrapped(f, whole=True):
    ''' Decorator function which calls Shape.wrap on every argument
        in f(self, *args), then calls f as usual.  The result is a field when all it is made of are fields (and
        numbers); with whole=False, as the first of them is (a remapped body is a body)
    '''
    def g(*args):
        args = [Shape.wrap(a) for a in args]
        out = f(*args)
        out._kind = _combined_kind(args if whole else args[:1])
        for a in args:                  # (a field made of fields is about what the first of them is about)
            o = a._field_origin
            if o is not None:
                out._field_origin = o
                break
        return out
    return g

class Shape:
    # What this is, for the model tree's icons and colours (fieldes.kinds): 'field' for what is made to be a field
    # (a distance, a ramp, noise ...), 'const' for a number made into a tree, 'point' / 'surface' for those, and None
    # for anything else, which is a body (3D, or 2D when it has no z)
    _kind = None
    # Where a field made from a point or a body is "about" (the point, the body, or three numbers): the field viewer starts there
    _field_origin = None

    def __init__(self, ptr):
        ''' Builds a Shape from a raw pointer.

            It is unlikely that you want to call this by hand; consider using
            X/Y/Z, the @shape decorator, or functions in fieldes.stdlib instead
        '''
        if not isinstance(ptr, int):
            raise TypeError(
                "Must call Shape.__init__ with a c_void_p returned " +
                "from a FielDes library function.  It is unlikely "
                "that you want to call this by hand; consider using " +
                "Shape.X() (and Y and Z), or the @shape decorator")
        self.ptr = ptr

    def __del__(self):
        lib.libfive_tree_delete(self.ptr)

    @classmethod
    def new(cls, op, *args):
        if isinstance(op, str):
            op = op.encode('ascii')
        if isinstance(op, bytes):
            op = lib.libfive_opcode_enum(op)
        if op == -1:
            raise RuntimeError("Invalid opcode")
        num_args = lib.libfive_opcode_args(op)
        if len(args) != num_args:
            raise RuntimeError("Error: opcode {} takes {} arguments"
                    .format(op, num_args))
        if num_args == 0:
            return cls(lib.libfive_tree_nullary(op))
        elif num_args == 1:
            return cls(lib.libfive_tree_unary(op, args[0]))
        elif num_args == 2:
            return cls(lib.libfive_tree_binary(op, args[0], args[1]))

    @classmethod
    def wrap(cls, t):
        ''' Ensures that the input argument is a Shape, converting constants
        '''
        if isinstance(t, numbers.Number):
            out = cls(lib.libfive_tree_const(t))
            out._kind = 'const'
            return out
        elif isinstance(t, cls):
            return t
        else:
            raise RuntimeError("Cannot convert {} into a Shape".format(t))

    @classmethod
    def X(cls):
        return cls.new('var-x')
    @classmethod
    def Y(cls):
        return cls.new('var-y')
    @classmethod
    def Z(cls):
        return cls.new('var-z')

    def __repr__(self):
        return "<shape@0x{:x}>".format(self.ptr)

    def __str__(self):
        s = lib.libfive_tree_print(self.ptr)
        try:
            return ctypes.c_char_p(s).value.decode('ascii')
        finally:
            lib.libfive_free_str(ctypes.cast(s, ctypes.c_char_p))

    def __bool__(self):
        raise RuntimeError("Cannot check truthiness of Shape")

    @_wrapped
    def __add__(self, other):
        return Shape.new('add', self.ptr, other.ptr)

    @_wrapped
    def __radd__(self, other):
        return Shape.new('add', other.ptr, self.ptr)

    @_wrapped
    def __sub__(self, other):
        return Shape.new('sub', self.ptr, other.ptr)

    @_wrapped
    def __rsub__(self, other):
        return Shape.new('sub', other.ptr, self.ptr)

    @_wrapped
    def __mul__(self, other):
        return Shape.new('mul', self.ptr, other.ptr)

    @_wrapped
    def __rmul__(self, other):
        return Shape.new('mul', other.ptr, self.ptr)

    @_wrapped
    def __mod__(self, other):
        return Shape.new('mod', self.ptr, other.ptr)

    @_wrapped
    def __rmod__(self, other):
        return Shape.new('mod', other.ptr, self.ptr)

    @_wrapped
    def __pow__(self, other):
        return Shape.new('pow', self.ptr, other.ptr)

    @_wrapped
    def __rpow__(self, other):
        return Shape.new('pow', other.ptr, self.ptr)

    @_wrapped
    def __truediv__(self, other):
        return Shape.new('div', self.ptr, other.ptr)

    def __neg__(self):
        return self._unary('neg')

    def __pos__(self):
        return self

    @_wrapped
    def __rtruediv__(self, other):
        return Shape.new('div', other.ptr, self.ptr)

    @staticmethod
    def _remapped(self, x_, y_, z_):
        return Shape(lib.libfive_tree_remap(
            self.ptr, x_.ptr, y_.ptr, z_.ptr))

    def remap(self, x_, y_, z_):
        ''' Performs the remapping f(x_, y_, z_)
        '''
        args = [Shape.wrap(a) for a in (self, x_, y_, z_)]
        out = Shape._remapped(*args)
        out._kind = self._kind if self._kind in ('field', 'const') else None
        return out

    def __eq__(self, other):
        raise RuntimeError("Shape does not support equality comparisons")

    @classmethod
    def var(cls):
        return cls.new('var-free')

    def with_constant_vars(self):
        return Shape.new('const-var', self.ptr)

    def lock(self):
        return self.with_constant_vars()

    def optimized(self):
        return Shape(lib.libfive_tree_optimized(self.ptr));

    def _unary(self, op):
        out = Shape.new(op, self.ptr)
        out._kind = self._kind if self._kind in ('field', 'const') else None
        out._field_origin = self._field_origin
        return out

    def sqrt(self):
        return self._unary('sqrt')

    @_wrapped
    def pow(self, other):
        return Shape.new('pow', self.ptr, other.ptr)

    def sin(self):
        return self._unary('sin')
    def cos(self):
        return self._unary('cos')
    def tan(self):
        return self._unary('tan')
    def asin(self):
        return self._unary('asin')
    def acos(self):
        return self._unary('acos')
    def atan(self):
        return self._unary('atan')
    def exp(self):
        return self._unary('exp')
    def log(self):
        return self._unary('log')
    def square(self):
        return self._unary('square')
    def abs(self):
        return self._unary('abs')
    def __abs__(self):
        return self._unary('abs')

    @_wrapped
    def atan2(self, other):
        return Shape.new('atan2', self.ptr, other.ptr)

    @_wrapped
    def nth_root(self, other):
        return Shape.new('nth-root', self.ptr, other.ptr)

    @_wrapped
    def nanfill(self, other):
        return Shape.new('nanfill', self.ptr, other.ptr)

    @_wrapped
    def min(self, other):
        return Shape.new('min', self.ptr, other.ptr)

    @_wrapped
    def max(self, other):
        return Shape.new('max', self.ptr, other.ptr)

    def _display(self):
        ''' What is drawn for this shape: itself, except where it is of a kind that has no body to draw -- a 2D shape
            (drawn flat), a surface (a thin sheet), a point (a small ball), a field (nothing: the section viewer shows it): see
            fieldes.stdlib.points '''
        from fieldes.stdlib import points
        return points.displayed(self)

    def __call__(self, x, y, z):
        if all([isinstance(c, numbers.Number) for c in [x, y, z]]):
            return lib.libfive_tree_eval_f(self.ptr, libfive_vec3_t(x, y, z))
        else:
            raise RuntimeError("x/y/z arguments must be numbers")

    def call(self, fn, *args, **kwargs):
        ''' Calls the given function with self as the first argument.
            This is convenient to chain transforms without deep call trees.
        '''
        return fn(self, *args, **kwargs)

    @_wrapped
    def compare(self, other):
        return Shape.new('compare', self.ptr, other.ptr)

    def save_stl(self, filename, xyz_min=(-10,-10,-10), xyz_max=(10,10,10),
                 resolution=10, quality=8):
        ''' Converts this Shape into a mesh and saves it as an STL file.

            xyz_min/max are three-element lists of corner positions
            resolution is the reciprocal of minimum feature size
                (larger values = higher resolution = slower)
            quality is the negative order of magnitude of maximum error
        '''
        region = libfive_region_t(*[libfive_interval_t(a, b) for a, b
                                    in zip(xyz_min, xyz_max)])
        # (the C function reads trees until a null pointer: without that terminator it read whatever lay after
        # the array, and crashed now and then)
        ptr_array = [self.ptr, None]
        trees = (libfive_tree * len(ptr_array))(*ptr_array)
        lib.libfive_tree_save_meshes(trees, region, resolution, quality,
                                   filename.encode('ascii'))

    def get_mesh(self, xyz_min=(-10,-10,-10), xyz_max=(10,10,10),
                 resolution=10, algorithm='dc'):
        ''' Converts this Shape into a mesh, and returns that mesh in
            the form of a face-vertex mesh.

        Options are the same as in save_stl(). `algorithm` selects the
        meshing algorithm: 'dc' (dual contouring, default, fastest),
        'simplex', or 'hybrid'. The latter two are slower but more
        robust for STEP-imported oracle shapes, where dual contouring
        has been found to fragment meshes even when the underlying
        field is provably correct at every queried point.

        Returns:
        verts - List of (x, y, z) vertices
        tris - List of (a, b, c) indices into verts
        '''
        region = libfive_region_t(*[libfive_interval_t(a, b) for a, b
                                    in zip(xyz_min, xyz_max)])
        if algorithm == 'dc':
            mesh_p = lib.libfive_tree_render_mesh(self.ptr, region, resolution)
        else:
            algo_id = {'dc': 0, 'simplex': 1, 'hybrid': 2}[algorithm]
            mesh_p = lib.libfive_tree_render_mesh_algo(self.ptr, region, resolution,
                                                        algo_id, 1e-8, 8)
        mesh = mesh_p[0]
        tris = []
        for i in range(mesh.tri_count):
            f = mesh.tris[i]
            tris.append((f.a, f.b, f.c))
        verts = []
        for i in range(mesh.vert_count):
            v = mesh.verts[i]
            verts.append((v.x, v.y, v.z))
        lib.libfive_mesh_delete(mesh_p)
        return verts, tris

################################################################################

def shape(f):
    ''' Decorator which converts a target function into a Shape
    '''
    return f(Shape.X(), Shape.Y(), Shape.Z())

# Hotpatch every transform (which take a shape as their first arguments)
# into the Shape as methods, to make things easier to chain
import fieldes.stdlib.transforms as _transforms
for (_name, _f) in _transforms.__dict__.items():
    if callable(_f):
        setattr(Shape, _name, _f)

# Hot-patch a few CSG functions onto the Shape class
import fieldes.stdlib.csg as _csg
for _name in ['union', 'intersection', 'difference', 'offset']:
    setattr(Shape, _name, getattr(_csg, _name))

# What those functions do with a shape that has an excluded region (see fieldes.stdlib.excluded)
from fieldes.stdlib import excluded as _excluded
_excluded.install_methods()
