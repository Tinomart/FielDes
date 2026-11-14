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

# The most tetrahedra a default element size may allow (a bound on the memory and the time of a default mesh; the elements are always shown, H198, so this is no limit of the viewer)
DRAWABLE = 4000000

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


def default_element_size(shape, lo, hi, divisions, budget, thin=True, notes=None, quadratic=False, walls=None):
    ''' The element size of an analysis that was not given one: the part's longest side over `divisions`, or -- when that makes more than `budget` tetrahedra -- the size at which
        the part has `budget` of them.  And never a size at which the part could have more tetrahedra than the result card can draw (H186, the user, 2026-11-12: "so that in no
        circumstance it overshoots the threshold at which the user cannot see the element button"): the part lies in its box, and the box holds at most so many tetrahedra at an
        element size -- a part whose volume the coarse samples miss (a fine lattice, a thin shell) is held by that bound too.
        And a size at which the part keeps its thin walls (`thin`, H193): the lattice that the tetrahedra are cut from loses a wall thinner than about 0.7 of its spacing -- a shell box with walls of
        1.2 mm was refused at the size the rules above gave it -- so where the kernel finds material that the lattice missed (meshing.py thin_size) the size is half the thickness of the thinnest walls
        a tenth of the surface is thinner than, within the budget as far as that holds them and never more than four times it (a thin wall asks for more elements than a solid body of its size, but the solve and the
        drawing of them are what the budget is for).  `quadratic`: the 10-node element, which reads a wall well with 1.5 elements across it (the linear one is made two across).  `notes` (a list) gets the sentence that says so when the size was changed for it.  `walls` (a dict) gets the thickness of the walls that a tenth of the surface is thinner than, as 'wall', where the kernel
        could measure it (the analyses tell of linear tetrahedra that are few across a wall) '''
    size = max(float(hi[i]) - float(lo[i]) for i in range(3))
    h = size / divisions
    volume = estimated_volume(shape, lo, hi)
    if volume and volume > 0.0 and tetrahedra(volume, h) > budget:
        h = (TETS_PER_CUBE * volume / budget) ** (1.0 / 3.0)
    box = 1.0
    for i in range(3):
        box *= max(float(hi[i]) - float(lo[i]), 0.0)
    h = max(h, (TETS_PER_CUBE * box / DRAWABLE) ** (1.0 / 3.0))
    if thin:
        from fieldes.stdlib.meshing import thin_size, THIN_COARSEST
        h, note = thin_size(shape, lo, hi, h, budget, TETS_PER_CUBE, min(DRAWABLE, 4 * budget), THIN_COARSEST if quadratic else None, walls)
        if note and notes is not None:
            notes.append(note)
    return h
