# Texture: a pattern of small bumps and pits laid over the surface of a body.
#
#   row 1   patterns made of noise     grit (sandpaper)  ·  stipple (the orange peel of a moulded part)  ·  leather (grooves round cells)  ·  ribs
#   row 2   periodic patterns, regions dimples (an egg carton)  ·  knurl (a grip)  ·  the top face only  ·  a belt round the middle only
#   row 3   how it is laid             side='in'  ·  side='out'  ·  a pattern of your own  ·  a depth that is a field
#
# texture(body, pattern, depth, size, region, fade, seed, axis, radius, side) returns a body: the field of `body`, moved in and out by the pattern.
#
#   pattern   'grit', 'stipple', 'leather', 'ribs', 'dimples', 'knurl' -- or a field of your own, about -1 .. 1 (the pattern of the third tile of row 3)
#   size      the grain or the pitch (mm);  depth  the height from a pit to a bump (mm)
#   region    where it goes: a surface picked on the body (select_surface), or a body that holds the places to texture; it fades out over `fade` mm
#             (the size, by default) so that no step is left at the edge
#   side      'both' bumps outside and pits inside the surface (the default), 'in' only cuts into it (the part stays inside its size), 'out' only adds
#   axis      the way 'ribs' run across and 'knurl' goes round;  radius  what a knurl goes round (it sets how many teeth there are)
#   seed      another pattern of the same kind
#
# Where is what (x to the right, y away from you; the names are the names of the rows in the model tree -- click a row and its block is lit up;
# `veins` uses a pattern of your own and `graded` a depth of your own):
#
#   y = 80    cut_in       added        veins         graded
#   y = 40    dimples      knurl        top_only      belt_only
#   y =  0    grit         stipple      leather       ribs
#             x = 0        x = 40       x = 80        x = 120
#
# A texture is made of the field of the body, so the render has to be fine enough to show it: about four samples to the size.  The grains here are 2.5 mm,
# the resolution 3 (samples per mm).  Blocks are 30 mm across and 40 mm apart.  Change a number, or the pattern, and the block follows.
from fieldes import *

view.set_bounds([-22, -22, -6], [142, 102, 18], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

# ---- row 1: noise ------------------------------------------------------------------------------------------------------------------------------
grit_block = box((-15, -15, 0), (15, 15, 10))
grit = texture(grit_block, 'grit', depth=0.8, size=2.5)
grit

stipple_block = box((25, -15, 0), (55, 15, 10))
stipple = texture(stipple_block, 'stipple', depth=0.9, size=3.5)
stipple

leather_block = box((65, -15, 0), (95, 15, 10))
leather = texture(leather_block, 'leather', depth=0.8, size=6)
leather

# Ribs: grooves of a pitch of `size`, across the axis (here y: they run along x).
ribs_block = box((105, -15, 0), (135, 15, 10))
ribs = texture(ribs_block, 'ribs', depth=0.8, size=3, axis='y')
ribs

# ---- row 2: periodic patterns and regions --------------------------------------------------------------------------------------------------------
dimples_block = box((-15, 25, 0), (15, 55, 10))
dimples = texture(dimples_block, 'dimples', depth=1.0, size=4)
dimples

# A knurl goes round an axis (here z): give the radius it goes round, and the teeth are counted from it.  It is for the side of a post: on a flat end it turns
# into a fan, so a region (a box round the middle of the post, 2 mm in from each end) keeps it off the ends.
knurl_post = cylinder(14, 10, (40, 40, 0))
knurl_band = box((20, 20, 2), (60, 60, 8))
knurl = texture(knurl_post, 'knurl', depth=0.8, size=2.5, axis='z', radius=14, region=knurl_band, fade=1.5)
knurl

# Only on the top face: a surface picked on the body (a point on it, and how far the face may bend), the texture fades out over `fade` mm at its edge.
top_block = box((65, 25, 0), (95, 55, 10))
top_face = select_surface(top_block, (80, 40, 10))
top_only = texture(top_block, 'grit', depth=1.0, size=2.5, region=top_face, fade=2)
top_only

# Only inside a body: a belt (a box) round the middle of the block; whatever of the block is in it is textured.
belt_block = box((105, 25, 0), (135, 55, 10))
belt = box((100, 20, 3), (140, 60, 7))
belt_only = texture(belt_block, 'grit', depth=1.0, size=2.5, region=belt, fade=1.5)
belt_only

# ---- row 3: how it is laid -----------------------------------------------------------------------------------------------------------------------
# side='in' cuts only: the block stays inside its size.  side='out' adds only: the block grows by the depth.
in_block = box((-15, 65, 0), (15, 95, 10))
cut_in = texture(in_block, 'stipple', depth=1.2, size=3.5, side='in')
cut_in

out_block = box((25, 65, 0), (55, 95, 10))
added = texture(out_block, 'stipple', depth=1.2, size=3.5, side='out')
added

# A pattern of your own: any field, about -1 .. 1 (here a ridged noise: veins).
veins_pattern = noise_field(6, octaves=2, seed=7, kind='ridged')
veins_block = box((65, 65, 0), (95, 95, 10))
veins = texture(veins_block, veins_pattern, depth=1.2, size=1)
veins

# The depth is a field too: smooth at the left of this block, 1.5 mm deep at the right.
deepening = ramp(x_field(), (105, 135), (0.0, 1.5))
graded_block = box((105, 65, 0), (135, 95, 10))
graded = texture(graded_block, 'grit', depth=deepening, size=2.5)
graded
