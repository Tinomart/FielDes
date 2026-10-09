'''
A lattice that follows a surface: its cells lie on the surface, face its normal and are as big as asked all
along it -- the "conformal" lattice of nTop.

    from fieldes import *

    # a thin shell of the part filled with strut cells 6 mm wide: the cells run through its thickness
    skin = lattice_surface_conform(shell_outside(part, 4), cell_periodic('octet'), cell_thickness=None, cell_size=6)

    # a thin layer (3 mm, the default) of strut cells 6 mm wide on the surface of the part, standing out of it: the cells are
    # flattened to fit (stretch_cell=True, the default)
    layer = lattice_surface_conform(part, cell_periodic('octet'), side='outside', cell_size=6, radius=0.4)

    # the same, but the cells keep their own proportions (as deep as they are wide): 3 mm of each stands out of the surface, and the
    # rest of it, on the inside of the body, is cut off at the surface
    rivets = lattice_surface_conform(part, cell_periodic('octet'), side='outside', stretch_cell=False, cell_size=6, radius=0.6)

    # strut cells standing 6 mm out of the surface of the part
    ribs = lattice_surface_conform(part, cell_periodic('octet'), side='outside', cell_thickness=6, cell_size=6, radius=0.6)

    # ... or only over a face you picked (right-click it in FielDes, or select_surface())
    top = select_surface(part, seed=(12.5, 40.0, -3.0), angle=10)
    ribs = lattice_surface_conform(top, cell_periodic('bcc'), cell_thickness=6, cell_size=6, radius=0.6)

    part_with_ribs = union(part, ribs)

    # an open surface of no thickness -- a field that is zero on it (negative below, positive above) -- with a patch
    # of it: one layer of cells on its positive side, cut off at the edge of the patch
    wave = Shape.Z() - 8 * (0.12 * Shape.X()).sin()
    layer = lattice_surface_conform(wave, cell_periodic('bcc'), within=box_exact((-30, -20, -14), (30, 20, 14)),
                                    side='outside', cell_thickness=6)

    # a periodic surface instead of struts: a gyroid skin that follows the part, 6 mm periods, 1 mm walls
    texture = lattice_surface_conform(shell_outside(part, 4), cell_periodic('gyroid'), cell_thickness=None, cell_size=6, thickness=1.0)

A body's surface is what the cells are laid on.  Where a plain lattice() cuts a straight grid off at the surface,
here the grid is drawn on the surface itself: a row of cells runs along it and bends with it, round a cylinder,
over a fillet, along an S-shaped surface, and every cell has its top and bottom face parallel to the surface and its
sides along the surface normal -- the same face towards the normal everywhere.  The layer is `cell_thickness` thick (3 mm by
default: a thin layer, for riveting and the like), measured from the surface: with side='inside' (the default for a body) it goes into
the body, with side='outside' the cells stand out of the surface (ribbing standing on the part).  Its cells are flattened to fit
(`stretch_cell=True`); with `stretch_cell=False` they keep their own proportions, and only `cell_thickness` of them stands out of the
surface, the rest -- on the other side of it, the inside of the body -- being cut off there.  `cell_thickness=None` fills the body
instead: the cells run through its thickness, one layer for a thin shell or sheet, more where it is thicker (at most three cells
deep).  A *surface* -- a field that is only a surface, with no thickness (and so no other face, no rim) -- gets one layer, on the side
`side` names (`'outside'` is the side the field is positive on), cut off at the edge of the region given as `within=`.

It is made from the body's field and nothing else: the surface is where the field is zero, its normal the field's
gradient.  No mesh of the body is made and no distance is taken to one; the mesh is only what is drawn at the end.

How the cells are laid out
    The surface is first covered by ONE MESH OF QUADS, one quad to a cell, before any cell is made: its rows follow the surface's own
    directions -- along a sharp edge, round a hole, along a handle -- and its quads are `cell_size` wide where the surface lets them be.
    There are three layouts, for three kinds of surface, and which one is used is TOLD, never guessed from the field: a closed BODY, a
    SURFACE that is only a surface (cut by the region you give it), and a SELECTION (a patch picked on a surface with select_surface()).

    A BODY (a closed solid): the surface ends inside its box, and the mesh is CLOSED and covers ALL of it.  It is made from a cloud of
    points of the surface, in five steps, all from the field:

    1. points of the surface a third of a spacing apart (the centres of the cubes the surface passes through, put onto the surface along the
       field's gradient); two points are neighbours when the SURFACE joins them, not when they are close in space -- a hop is accepted if its
       middle lies on the surface, or if the chord put onto the surface is a connected curve of about its length -- so a thin wall or a gap
       between two points is never crossed; at a sharp edge a point is put ON the edge;
    2. a direction field with four-fold symmetry and a lattice position field over the points; sharp edges are lines the field follows, so
       the rows of cells run along them;
    3. lattice vertices where the position field puts them, made fine enough that the part of the surface nearest to each vertex is a disc
       and two vertices that are neighbours touch along ONE arc;
    4. a face for every three of those parts that meet at a place (one for four when four meet), taken in the order of how many points see
       them, as long as the faces keep making a surface: every edge on two faces, the faces round a vertex one fan, all going round the
       same way;
    5. a face with k corners is made into k quads -- a corner, the middle of the side after it, the middle of the face, the middle of the
       side before it -- the middle of a side being shared by the two faces that have it, so the quads fit edge to edge.  Finally the nodes
       are moved over the surface towards the middle of their neighbours and put back on it, a little at a time (a node on a sharp edge
       stays; no move may fold a cell or make a corner worse): the cells stay the same cells.

    NOTHING IS LEFT OUT.  Every separate piece of the surface is mapped on its own and checked on its own (four different corners to every
    cell, every edge on two cells, one fan round every vertex, one connected surface).  A piece too small for cells of `cell_size` -- a
    cavity inside a boss, a small island of the field -- is sampled finer, by itself, until it can have a closed map: its cells are then
    smaller than asked.  Two surfaces that the points join by a few hops (two walls closer together than the points can tell apart) are
    cut apart and laid out again.  A speck that the points found where the field comes near zero but never crosses it is not a surface: it
    is given up only after the field has been read on a fine grid round it and found to keep one sign.  The layout is made
    from several placements of the grid the samples are taken on, and the topology of what comes out is VOTED on: a map is used when
    two placements agree on how many separate surfaces there are and on their Euler number; a surface that the cells can resolve gives
    the same answer from every placement, one they cannot (a hole or a gap narrower than a cell, a wall thinner than one) gives answers
    that differ with where the samples fall, and then nothing is made and the error says so.  If every placement is refused, a piece
    that can be judged but is not a good map may be sampled once finer (cells half as big).  An error says which piece of the surface
    could not be mapped and where, and what to try (a cell size about half as big), and nothing is returned: a map that does not cover
    the whole surface, or that another map of the same surface contradicts, is never used.

    A SURFACE THAT IS ONLY A SURFACE -- a field that is zero on a sheet with no body behind it, which goes on past the region you give with
    within=: the layout has an edge where the region cuts it, and only there.  A scaffold of it is made (a triangle mesh by marching
    tetrahedra over the cubes the surface passes through, its vertices moved onto the surface and onto its sharp edges), a direction field
    and a lattice position field are solved over the scaffold, the triangles that lie in one square of the lattice make a region, and a
    region with k corners is made into k quads as above; the nodes are moved over the surface until the cells are even.  The sheet is laid
    out over a margin of two cells round the region, so that the region lies well inside it.

    A SELECTION is a surface, and is laid out as one, by the same method as a sheet: from the surface the picked patch makes -- the points
    the walk over it took, with their normals, as a field (the height above the patch along its normal) -- over the part of it that is by the
    patch, and only there.  The body it was picked on is not looked at, nor its other faces, nor how thick it is: a shell, a plate and a solid
    are the same to it, and the cells cover the patch and nothing else (their edge follows the patch's, to within a third of a cell).  The
    lattice stands on the side the surface faces (side='outside', `cell_thickness` thick).

    Every node is on the surface, and every point of a cell is put back on it too, so no strut lies in a hole or outside the body.  Cells
    are distorted wherever the surface cannot be flattened -- over a fillet, round the lip of a rim, across a dome -- and none is left out
    for that: a cell is as stretched, squeezed or bent as the surface makes it, and the beams of the unit cell follow.  Where a narrow
    fillet or a small step is narrower than a cell, or the rows of cells have to turn a corner, some cells are less regular.

cell
    a strut cell ('octet', 'bcc', 'cubic', 'kelvin', ...: see lattice()) -- `radius` is the strut radius.  The cell's
    beams are carried over to every cell of the grid: the lattice is a graph of straight beams, and the mesher
    measures the distance to the beams near a point, so it is as quick as a graph lattice.

    or a periodic surface ('gyroid', 'schwarz_p', 'diamond', 'neovius', 'lidinoid', 'split_p', 'iwp', 'frd',
    'fischer_koch_s') -- `thickness` is the wall of a sheet, or style='network' for the solid on one side of it.  The
    periodic function is evaluated in the coordinates of the cell a point is in (s, t along the surface, w through the
    layer, found from the four edges of the cell and the depth of the layer), one period to each cell on the surface and
    to each layer, so its sheets run through the layers and bend with the surface.  The ones made of cosines only
    (schwarz_p, neovius, iwp, frd) look the same after a quarter turn, so they join up where rows of cells meet; the
    others (gyroid, diamond, ...) have a seam there.  Rendering costs more than for struts: the sheets fill the layers.
    Where three or five cells meet the pattern is rougher; where the surface is flat enough, or one orientation of the
    texture on every face is fine, a plain union(part, lattice(shell_outside(part, depth), cell_periodic(...),
    cell_size=...)) has none of that.

The lattice reaches a little into the part (by a strut's radius) so that it fuses with it when you add the two.  It
makes no skin, adds no body and cuts nothing of the part: combine it with the part yourself.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import ctypes
import math
import numbers
import os

from fieldes.ffi import lib
from fieldes.shape import Shape
from fieldes.stdlib import lattices as _L
from fieldes.stdlib.fields import _shape_bounds, _script_vars
from fieldes.stdlib.selection import SurfaceSelection

__all__ = ['lattice_surface_conform']

# the periodic surfaces that can follow a surface (the numbers are the kernel's)
_TPMS_CODES = {'gyroid': 0, 'schwarz_p': 1, 'schwarz': 1, 'diamond': 2, 'schwarz_d': 2, 'neovius': 3, 'lidinoid': 4,
               'split_p': 5, 'iwp': 6, 'frd': 7, 'fischer_koch_s': 8}


def _viewport_resolution():
    ''' The resolution (samples per mm) the script has asked the viewport for so far, or None (outside the
        application, or not set yet) '''
    try:
        import _fieldes_host as host
        res = getattr(host, '__resolution', None)
        return float(res) if res else None
    except (ImportError, TypeError, ValueError):
        return None


def _say_doubts():
    ''' What the layout of the cells leaves in doubt (a map whose topology is not certain, holes it closed): the map is made and
        covers the whole surface, and this says where it may differ from the part '''
    if not hasattr(lib, 'libfive_lattice_last_warning'):
        return
    warning = lib.libfive_lattice_last_warning()
    if warning:
        print('lattice_surface_conform: ' + warning.decode('utf-8', 'replace'))


def lattice_surface_conform(surface_field, cell=None, cell_thickness=3.0, stretch_cell=True, cell_size=5.0, radius=None,
                            layers=None, side=None, blend=0.0, direction=None, bounds=None, thickness=None,
                            style='sheet', offset=0.0, invert=False, skin=0.0, within=None, grid_offset=0):
    ''' A lattice that follows a surface (see the module): its cells lie on it, as big as asked all along it, each
        with a face towards the surface normal -- filling the body the surface bounds (side='inside', the default) or
        standing out of it (side='outside').

        surface_field   the surface, as ONE argument whatever it is: a body (a closed solid: its surface), a surface
                        (a field that is zero on it, with no body behind it), or a select_surface(...) selection (the
                        patch picked on a body: a SURFACE, laid out as one -- a method of its own, that has nothing to do
                        with what is behind it or how thick that is -- the lattice is laid on that patch only)
        cell            what it is made of: cell_periodic('octet') (a strut cell: octet, bcc, cubic, kelvin ...; a
                        TPMS: gyroid, schwarz_p ...), cell_non_periodic(...) for a graph of random cells laid on the
                        surface, or cell_custom_truss(nodes, beams).  Default: cell_periodic('octet')
        cell_thickness  how thick the layer of cells is (mm), measured from the surface: how far the cells stand out of it
                        (side='outside') or go into the body (side='inside').  Default 3 mm: the lattice is a thin layer,
                        for riveting and the like.  None: for a body filled from inside, as deep as the body is under each
                        cell (a thin shell: its thickness; at most three cells); for a surface, a selection or side='outside',
                        one cell
        stretch_cell    True (the default): the cells are deformed to fit the thickness -- `cell_size` along the surface,
                        `cell_thickness` through it, so a thin layer has flat cells.  False: the cells keep their own
                        proportions (as deep as they are wide), and `cell_thickness` of them stands out of the surface;
                        the rest of each cell, on the other side of the surface (the inside of the body), is cut off there.
                        (`layers` then counts whole cells through the depth; a thickness over one cell makes more of them)
        within          where, besides: any shape, or a list of them, the lattice is kept inside it (default: everywhere on the surface)
        cell_size       mm along the surface
        thickness   the thickness of the cell's members, mm: the diameter of the beams of a strut cell or a non-periodic cell,
                    the wall of a TPMS sheet (below).  Default: beams 24 % of cell_size across (not more than half of the layer's
                    thickness), a sheet 15 % of cell_size
        radius      the same for beams, as a radius (half of thickness; give one of the two): a number, or a field (the
                    struts taper between the nodes).  Struts inside a body keep inside it.  Default: 12 % of cell_size, but not
                    more than a quarter of the layer's thickness (a thin layer has thin struts)
        layers      the number of cells through the depth (default: as many as fit, at least 1)
        side        'inside' (the default for a body: the lattice fills it) or 'outside' (it stands out of the surface: the
                    default for a selection or a surface, on the side its normal faces -- the side the field is positive on)
        blend       rounds the joints of struts
        direction   the way the rows of cells run where the surface gives them no way (a flat or smoothly curved part with
                    no edge to follow) (default: along x)
        bounds      ((x0, y0, z0), (x1, y1, z1)) of the surface, if its extent cannot be found (a field with no end: an
                    open surface).  Default: its extent, and if it has none, that of `within`
        grid_offset 0 (the default), 1, 2 or 3: which of four fixed grids of sample points the surface is laid out from.
                    The layout is made ONCE, from that grid, and is the same every time.  Where the surface has detail
                    about as small as the spacing of the points (a narrow neck, a thin wall, a tiny gap) the grid decides
                    which points are joined, so a part that does not close with one value may close with another, or give a
                    different count of holes: if the call says no closed map could be made, or the warning says the
                    topology may be off, try the other values (or a smaller cell_size), looking at what the part has there

        For a TPMS kind (gyroid, schwarz_p, diamond, neovius, lidinoid, split_p, iwp, frd, fischer_koch_s) the periodic
        surface follows the surface, one period to each cell on it and to each layer:
        thickness   the wall of a sheet, mm (default 15 % of cell_size)
        style       'sheet' (walls `thickness` thick), or 'network' (the solid on one side of the surface, grown by
                    `offset` mm; `invert=True` takes the other side)
        skin        a solid skin this deep (mm) against the faces of the layers

        Returns the lattice alone, as a shape: add it to the part with union(). '''
    # the surface: a body, a surface, or a selection of one
    region = within
    if isinstance(region, (list, tuple)):
        # (several shapes are several places: the lattice is kept where any of them is)
        parts = [Shape.wrap(r) for r in region]
        region = parts[0]
        for r in parts[1:]:
            region = region.min(r)
    patch = None            # (the field that says where the patch of a selection is)
    if isinstance(surface_field, SurfaceSelection):
        if region is not None:
            raise ValueError('lattice_surface_conform: the selection is where the lattice goes: do not give within= as well')
        # A selection is a SURFACE, and a surface is laid out as one: from the surface the picked patch makes (the points of the walk over
        # it, with their normals), and nothing of the body it was picked on, its other faces or how thick it is
        region = surface_field
        body = Shape.wrap(surface_field.surface)
        patch = surface_field.patch
    else:
        body = Shape.wrap(surface_field)
    surface = region
    if side is None:
        # (a surface has one side to stand on, the one it faces; a body is filled)
        side = 'outside' if patch is not None else 'inside'
    cellobj = _L._need_cell(cell, 'lattice_surface_conform', default=lambda: _L.cell_periodic('octet'))
    if cellobj.family == 'shape':
        raise ValueError("lattice_surface_conform: a cell_custom(region, geometry) cell is a box of geometry: it repeats on "
                         "a straight, cylindrical or spherical grid (lattice(..., cell_map=...)), it cannot follow a "
                         "surface.  Use cell_periodic() with a strut cell or a TPMS, cell_custom_truss(), or "
                         "cell_non_periodic()")
    if cellobj.family == 'planar':
        raise ValueError("lattice_surface_conform: a planar cell is not made of cells that can follow a surface: use "
                         "cell_periodic() with a strut cell or a TPMS, or cell_non_periodic()")
    if cell_thickness is not None and not float(cell_thickness) > 0:
        raise ValueError('lattice_surface_conform: cell_thickness must be positive (or None)')
    if not isinstance(stretch_cell, (bool, numbers.Integral)):
        raise ValueError('lattice_surface_conform: stretch_cell is True or False')
    stretch_cell = bool(stretch_cell)
    if side not in (None, 'inside', 'outside'):
        raise ValueError("lattice_surface_conform: side is 'inside' or 'outside'")
    if int(grid_offset) != grid_offset or not 0 <= int(grid_offset) <= 3:
        raise ValueError('lattice_surface_conform: grid_offset is 0, 1, 2 or 3')
    grid_offset = int(grid_offset)
    c = float(cell_size) if isinstance(cell_size, numbers.Number) else float(cell_size[0])
    if not c > 0:
        raise ValueError('lattice_surface_conform: cell_size must be positive')
    n_layers = int(layers) if layers else 0
    # How the layers sit: `depth` is the height the kernel gives the cells, `lift` how far from the surface their base is.  Stretched
    # cells are as high as the layer is thick, on the surface.  Cells of their own proportions are whole cells (as high as they are wide)
    # stacked so that the top of the stack is `cell_thickness` from the surface: they start on the other side of it, and are cut there
    depth = None if cell_thickness is None else float(cell_thickness)
    lift = 0.0
    if not stretch_cell:
        if depth is None:
            raise ValueError("lattice_surface_conform: stretch_cell=False needs a cell_thickness: how much of the cells stands out of "
                             "the surface (the rest of them, on the other side of it, is cut off)")
        n_layers = max(n_layers, 1, int(math.ceil(depth / c - 1e-9)))
        lift = depth - n_layers * c
        depth = n_layers * c
    elif patch is not None and depth is None:
        depth = c           # (a surface that is not a body has no depth of its own: one cell)
    key = _L._plain(cellobj)
    if isinstance(key, _L.TPMSEquation):
        raise ValueError("lattice_surface_conform: your own TPMS equation cannot follow a surface; the TPMS cells are {}"
                         .format(', '.join(sorted(_TPMS_CODES))))
    tpms = cellobj.family == 'tpms'
    if tpms and key not in _TPMS_CODES:
        raise ValueError('lattice_surface_conform: {!r} cannot follow a surface; the TPMS cells that can are {}'.format(
            key, ', '.join(sorted(_TPMS_CODES))))
    if style not in ('sheet', 'network'):
        raise ValueError("lattice_surface_conform: style is 'sheet' or 'network'")
    if not tpms:
        # (beams: thickness is their diameter, radius half of it)
        radius = _L._beam_radius('lattice_surface_conform', thickness, radius, None)

    if getattr(lib, 'libfive_surface_cells', None) is None:
        raise RuntimeError('this FielDes library is too old for lattice_surface_conform (libfive_surface_cells missing)')
    if cellobj.family == 'foam':
        # (cells that do not repeat: a random graph on the surface, its beams centred on it)
        try:
            flo, fhi = _shape_bounds(body, bounds)
        except ValueError:
            if bounds is not None or surface is None:
                raise
            flo, fhi = _shape_bounds(surface, None)
        r = float(radius) if isinstance(radius, numbers.Number) else (radius if radius is not None else 0.12 * c)
        lat = _L._surface_lattice(body, c, radius=r, pattern='triangle' if cellobj.style == 'delaunay' else 'voronoi',
                                  seed=cellobj.seed, blend=blend, bounds=(flo, fhi))
        return _finish(lat, body, surface, side, cell_thickness, c, flo, fhi, False)
    if tpms:
        if getattr(lib, 'libfive_surface_tpms', None) is None:
            raise RuntimeError('this FielDes library is too old for a TPMS on a surface (libfive_surface_tpms missing)')
        beams, margin = [], 0
    else:
        beams, margin = _L.unit_cell_beams(cellobj)
    if margin > 0 or getattr(_L.unit_cell_beams, 'radii', None):
        raise ValueError('lattice_surface_conform: a unit cell that is not mirror-symmetric, or whose beams have radii '
                         'of their own, cannot be carried over to the cells of a surface')
    # Which layout lays the cells out is told to the kernel, not guessed from the field: a selection is a patch of a surface (2), a surface
    # that has no extent of its own is cut by its region (1), a closed body is a body (0)
    layout = 0
    if patch is not None:
        # (the box the patch lies in, a cell round it)
        plo, phi = surface._bounds
        lo = tuple(float(plo[i]) - c for i in range(3))
        hi = tuple(float(phi[i]) + c for i in range(3))
        layout = 2
    else:
        # the box the cells are wanted in: the body's (or, for a surface with no end, the region's, or `bounds`)
        try:
            lo, hi = _shape_bounds(body, bounds)
        except ValueError:
            if bounds is not None or surface is None:
                raise
            lo, hi = _shape_bounds(surface, None)
            layout = 1
    lo3 = (ctypes.c_double * 3)(*[float(a) for a in lo])
    hi3 = (ctypes.c_double * 3)(*[float(b) for b in hi])
    d3 = (ctypes.c_double * 3)(*([float(x) for x in direction] if direction is not None else (0.0, 0.0, 0.0)))
    unit = (ctypes.c_float * (6 * len(beams)))(*[float(x) for a, b in beams for x in tuple(a) + tuple(b)])
    info = (ctypes.c_double * 10)()
    known = _script_vars()
    if tpms:
        wall = float(thickness) if thickness is not None else 0.15 * c
        if not wall > 0:
            raise ValueError('lattice_surface_conform: thickness must be positive')
        code_style = 0 if style == 'sheet' else (2 if invert else 1)
        targs = (lo3, hi3, d3, grid_offset, layout, patch.ptr if patch is not None else None, c, float(depth) if depth else 0.0,
                 n_layers, 1.0 if side == 'outside' else -1.0, lift,
                 _TPMS_CODES[key], wall, code_style, float(offset), float(skin), info)
        if known is not None:
            tptr = lib.libfive_surface_tpms(body.ptr, known[0], known[1], known[2], *targs)
        else:
            tptr = lib.libfive_surface_tpms(body.ptr, None, None, 0, *targs)
        if not tptr:
            raise ValueError('lattice_surface_conform: ' + lib.libfive_lattice_last_error().decode('utf-8', 'replace'))
        _say_doubts()
        lat = Shape(tptr)
        if os.environ.get('FIELDES_SC_STATS'):
            print('[conformal] {} cells, {} nodes, TPMS {} {}, wall {:g}, layers {} of {:.2f} mm'.format(
                int(info[0]), int(info[2]), key, style, wall, int(info[8]), info[9]))
        view_res = _viewport_resolution()
        if view_res and style == 'sheet' and wall * view_res < 3.0:
            print("lattice_surface_conform: its walls are {:g} mm thick: {:.1f} samples at the viewport's resolution "
                  "({:g} per mm), and a wall needs three or four to show: use a thicker wall or a finer "
                  "view.set_resolution() on a smaller region".format(wall, wall * view_res, view_res))
        return _finish(lat, body, surface, side, cell_thickness, c, lo, hi, not stretch_cell)
    if radius is None and stretch_cell and depth:
        # (the struts of a thin layer: a quarter of its thickness across, not more than 12 % of the cell -- thinner ones are finer than the
        # viewport can draw, and come out as specks)
        radius = min(0.12 * c, 0.25 * float(depth))
    radius_in = float(radius) if isinstance(radius, numbers.Number) else 0.0
    args = (d3, grid_offset, layout, patch.ptr if patch is not None else None, c, unit, len(beams), float(depth) if depth else 0.0,
            n_layers, radius_in,
            1.0 if side == 'outside' else -1.0, lift, info)
    if known is not None:
        ptr = lib.libfive_surface_cells(body.ptr, known[0], known[1], known[2], lo3, hi3, *args)
    else:
        ptr = lib.libfive_surface_cells(body.ptr, None, None, 0, lo3, hi3, *args)
    g = _L._graph_from_c(ptr, 'lattice_surface_conform')
    _say_doubts()
    cells = int(info[0])
    thickness, used_layers = info[9], int(info[8])
    if radius is None or not isinstance(radius, (numbers.Number, Shape)):
        radius = info[7]
    elif isinstance(radius, numbers.Number):
        radius = float(radius)
    if os.environ.get('FIELDES_SC_STATS'):
        print('[conformal] {} cells, {} nodes, {} beams, layers {} of {:.2f} mm, radius {:.2f}'.format(
            cells, int(info[2]), int(info[3]), used_layers, thickness, info[7]))
    if isinstance(radius, float):
        view_res = _viewport_resolution()
        if view_res and 2.0 * radius * view_res < 3.0:
            print("lattice_surface_conform: its struts are {:.2g} mm thick: {:.1f} samples at the viewport's "
                  "resolution ({:g} per mm), and a lattice needs three or four to show: use a larger radius or "
                  "cell_size, or a finer view.set_resolution() on a smaller region".format(
                      2.0 * radius, 2.0 * radius * view_res, view_res))
    lat = _L.graph_lattice(g.nodes, g.beams, radius, blend)

    return _finish(lat, body, surface, side, cell_thickness, c, lo, hi, not stretch_cell)


def _finish(lat, body, surface, side, thickness, c, lo, hi, trim):
    ''' The lattice cut to the part of the surface asked for, with the box it lies in.  `trim`: cells of their own proportions reach
        through the surface to the side the layer does not go to, and are cut off there (at the inside of the body, for a layer that
        stands out of it) '''
    if trim:
        # (the surface is where the field of `body` is zero, it is positive outside: an outward layer keeps what is outside)
        lat = lat.max(-body) if side == 'outside' else lat.max(body)
    if isinstance(surface, SurfaceSelection):
        # (nothing is cut: the cells were laid out over the patch and nowhere else, and stand on it -- a cut through a standing cell would
        # leave its upper part in the air)
        pass
    elif surface is not None:
        lat = lat.max(Shape.wrap(surface))

    box = getattr(surface, '_bounds', None) or getattr(body, '_bounds', None) or (lo, hi)
    if box:
        pad = (float(thickness) if thickness else c) + c if side == 'outside' else c
        lat._bounds = (tuple(box[0][i] - pad for i in range(3)), tuple(box[1][i] + pad for i in range(3)))
    return lat
