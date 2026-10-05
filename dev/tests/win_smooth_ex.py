from fieldes import *

view.set_bounds((-5, -5, -5), (30, 25, 25))
view.set_quality(8)
view.set_resolution(10)

box_1 = box_exact((0, 0, 0), (20, 20, 20))
region_1 = sphere(6, (20, 10, 10))
excluded_1 = exclude(box_1, region_1)
smooth_1 = smooth(excluded_1, 1.0, 3)
smooth_1
