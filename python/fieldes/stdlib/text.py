"""
Python library of FielDes, built on the libfive CAD kernel

Originally generated from the C++ standard library (kernel/stdlib/libfive_stdlib.h) by the libfive
project; the generator is not part of FielDes, so this file is now maintained by hand.

This is fieldes.stdlib.text
"""

from fieldes.ffi import libfive_tree, tfloat, tvec2, tvec3, stdlib
from fieldes.shape import Shape

import ctypes

stdlib.text.argtypes = [ctypes.c_char_p, tvec2]
stdlib.text.restype = libfive_tree
def text(txt, pos=(0, 0)):
    """ Returns the given text, rendered in a custom f-rep font
        (with a character height of 1)
    """
    args = [txt.encode('latin1'), list([Shape.wrap(i) for i in pos])]
    return Shape(stdlib.text(
        args[0],
        tvec2(*[a.ptr for a in args[1]])))
