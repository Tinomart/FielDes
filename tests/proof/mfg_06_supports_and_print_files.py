# Manufacturing 6: does it support an overhang it cannot fix, and write the files a printer needs?
#
# THE PART.  The ledge of mfg_03 (a wall with a slab sticking out, 20 mm long, 20 up).  The constraint asks overhang 45 and allows supports
# (support='block'), but the shell may change only 5 % of the part (max_change=0.05).
#
# WHAT IT SHOULD DO.
#   1. The gusset that would fix the overhang (5083 mm^3 by hand, see mfg_03) is 25 % of the part's 20 000 mm^3, more than the 5 % allowed: it
#      should NOT be made, and the output should say why (it should say about 25 % against 5 %).
#   2. Instead a SUPPORT, a block straight under the slab, from the plate (z = 0) to 0.3 mm under the slab (support_gap), in the air between the
#      wall and the strip kept free by no_support (x 25..31): a block about 15 x 40 x 19.7 = 11 800 mm^3 at most, a little less because of the
#      gap along the wall.  It is a SEPARATE body (orange).
#   3. The files are written next to this script, in the folder `out`:
#        print.3mf    three objects: the part, its supports and the no-support volume (the strip)
#        layers.dxf   the contours of every layer of the part, every 1 mm: 40 layers of closed polylines (the part is 40 high)
#        slice.svg    the layer at z = 10: the wall alone, 10 x 40 = 400 mm^2 (the slab is higher up)
#   The output pane lists the files with their sizes, so that you can open them: the 3MF in a slicer, the DXF in a CAD or laser program,
#   the SVG in a browser.
#
# The part is shown at the back and the result, with its supports, in front.
import os
from fieldes import *

view.set_bounds([-5, -55, -5], [45, 45, 45], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

ledge = union(box((0, 0, 0), (10, 40, 40)), box((10, 0, 20), (30, 40, 25)))
strip = box((25, -1, -1), (31, 41, 41))
printer = manufacturing_constraint(overhang=45, support='block', support_gap=0.3, no_support=[strip])
result = manufacturing_optimization(ledge, constraints=[printer], max_support_height=25, max_change=0.05)

os.makedirs('out', exist_ok=True)
export_print(result, 'out/print.3mf')
export_layers(result, 'out/layers.dxf', layer_height=1.0)
export_slice(result, 'out/slice.svg', 10.0)
for name in sorted(os.listdir('out')):
    print('written: out/%s, %d bytes' % (name, os.path.getsize(os.path.join('out', name))))
print('support volume: %.0f mm^3' % result.support_volume)
original = move(ledge, (0, -50, 0))
original
result
