'''
The element size an analysis uses when it is not told one.

The default is the part's longest side over a number of elements (60 for the tetrahedra of a static analysis, 40 for the other analyses, half of that for the 10-node
elements), so that a slender or thin part is resolved by that many elements along its length.  For a part that is SOLID -- a cube -- that is a great many elements: a cube
0.4 mm on a side had 2 678 400 tetrahedra, 18.6 s of solving and a 150 MB result file.  So the default is larger when the tetrahedra it would make are more than a budget for
the analysis (a million for a static analysis, fewer for those that cost more to the element): the size at which the part has that many.

The number of tetrahedra is estimated from the part's volume -- sampled on a coarse grid over its box -- and the tetrahedra that isosurface stuffing makes, about twelve to the cube
of an element.  A part that is thin or slender (a plate, a lattice, a shell) has little volume beside its box and is not changed by the rule.  An element_size that is given is used
as it is: nothing limits it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
__all__ = []

# Tetrahedra to the cube of the element size in the meshes of isosurface stuffing (measured on a cube: 2 678 400 tetrahedra at an element a sixtieth of its side, 12.4 to the cube)
TETS_PER_CUBE = 12.0
# The grid the volume is estimated on (the number of samples along one edge of the part's box)
SAMPLES = 24


def estimated_volume(shape, lo, hi):
    ''' The volume of `shape` in the box (lo, hi), from the share of a coarse grid of samples that lies inside it (None when it cannot be evaluated) '''
    try:
        from fieldes.stdlib.fields import sample_grid
        points, values = sample_grid(shape, lo, hi, SAMPLES)
    except Exception:
        return None
    if not values:
        return None
    inside = sum(1 for v in values if v < 0)
    box = 1.0
    for i in range(3):
        box *= float(hi[i]) - float(lo[i])
    return box * inside / len(values)


def tetrahedra(volume, element_size):
    ''' About how many tetrahedra the mesh of a part of this volume has at this element size '''
    return TETS_PER_CUBE * volume / (element_size ** 3)


def default_element_size(shape, lo, hi, divisions, budget):
    ''' The element size of an analysis that was not given one: the part's longest side over `divisions`, or -- when that makes more than `budget` tetrahedra -- the size at which
        the part has `budget` of them '''
    size = max(float(hi[i]) - float(lo[i]) for i in range(3))
    h = size / divisions
    volume = estimated_volume(shape, lo, hi)
    if not volume or volume <= 0.0:
        return h
    if tetrahedra(volume, h) <= budget:
        return h
    return (TETS_PER_CUBE * volume / budget) ** (1.0 / 3.0)
