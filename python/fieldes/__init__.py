'''
FielDes: field-driven design.

A script starts with

    from fieldes import *

which brings in everything the library offers (shapes, CSG, transforms, STEP and
mesh import, fields and regressions, lattices, analysis, FEA and thermal solvers,
topology optimisation, ...) and `view`, the viewport settings of the application
(view.set_bounds, view.set_resolution, view.set_quality).

The kernel this builds on is libfive by Matt Keeter (Mozilla Public License 2.0).
'''

import fieldes.shape
import fieldes.stdlib as _stdlib
from fieldes.stdlib import *
from fieldes.shape import Shape
from fieldes import view

__all__ = [n for n in dir(_stdlib) if not n.startswith('_')] + ['Shape', 'view']
