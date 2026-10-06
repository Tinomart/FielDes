# Custom blocks in the application: the blocks folder is chosen (and watched), a script uses them, the menus offer them
from fieldes import *

view.set_bounds([-30, -30, -5], [70, 50, 25])
view.set_resolution(4)
plate = box_exact((0, 0, 0), (60, 40, 6))
part = twice(plate)
part
