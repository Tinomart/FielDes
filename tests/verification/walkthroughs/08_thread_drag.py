from fieldes import *

# A block with a threaded hole in it: the body of the thread drag of tests/verification/00_START_HERE.py (item (d) of the long list).
# The rod is made by its radius and its length and placed by its one position, which is the middle of its base; it is hidden, and the
# difference is what you press and drag.

view.set_bounds([-14, -14, -3], [14, 14, 13], auto_scale=True)
view.set_quality(8)
view.set_resolution(10)

block = box((24, 24, 10), position=(0, 0, 5))
screw = threaded_rod(4, 14, position=(0, 0, -2))
nut = difference(block, screw)
nut
