# Fields everywhere: wherever a number goes, a field goes.
#
# A field is a number at every point of space: a distance, a ramp, noise, the stress of an analysis.  Every function of
# the library that takes a size, a thickness, a radius or a spacing takes a field in its place, and evaluates it where it
# needs it (nothing is sampled onto a grid).  Three bodies here, side by side, each driven by a field:
#
#   1. a plate grown by an amount that is 4 mm near an anchor point and 0.3 mm far from it   (offset)
#   2. a cylinder and a block joined by a blend that is sharp at one end and 8 mm round at the other   (smooth_union)
#   3. a gyroid whose cells are 16 mm at one end and 6 mm at the other   (lattice, cell_size)
#
# No STEP file is needed.  Change a number, or swap a field for another one (noise_field(...), distance_to_plane(...),
# depth_below(...), a result of an analysis ...), and the body follows.
from fieldes import *

view.set_bounds([-10, -10, -10], [225, 55, 45])
view.set_resolution(4)
view.set_quality(8)

# 1. A plate, grown more near the anchor.  The anchor is a point: anything that wants a coordinate takes it.
plate = box_exact((0, 0, 0), (60, 40, 6))
anchor = point(52, 8, 6)
anchor = handles(anchor, move=(var(0), var(-2.37372), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
anchor
swell = ramp(distance_to_point(anchor), (0, 60), (4.0, 0.3))
grown = offset(plate, swell)
grown

# 2. The blend radius of a smooth union as a field of x: 0.5 mm at the left, 8 mm at the right.
post = cylinder_z(10, 30, (105, 20, 0))
block = box_exact((80, 8, 0), (140, 32, 10))
blend = ramp(x_field(), (80, 140), (0.5, 8.0))
cells = ramp(x_field(), (160, 220), (16.0, 6.0))
joined = smooth_union(post, block, blend)
joined = expose(joined, [
    var(105.0), var(20.0), var(10.0), var(30.0), var(0.0), var(110.0), var(30.0), var(20.0),
    var(12.0), var(5.0), var(5.0), var(80.0), var(10.0),
])
joined = handles(joined, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
joined
# 3. A lattice whose cell size is a field.  Big cells where the field is large, small ones where it is small;
#    a graded cell size blends lattices a factor of two apart, so it can change by a factor of up to 16.
core = box_exact((160, 0, 0), (220, 40, 30))
foam = lattice(core, cell_periodic('gyroid'), cell_size=cells, thickness=1.0)
foam = expose(foam, [
    var(0.5), var(160.0), var(1.0), var(0.5), var(1.0), var(1.0), var(0.5), var(2.0),
    var(1.0), var(190.0), var(30.0), var(20.0), var(20.0), var(15.0), var(15.0),
])
foam = handles(foam, move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), scale=(var(1), var(1), var(1)))
foam

