# One part that needs a gusset under a ledge (material added) and a teardrop over a bore (material cut), with a constraint of its own for each: the
# result is drawn as ONE body coloured by the change -- the part's own colour, red where it was added, blue where it was cut -- and the change map says +1, -1 and 0 at
# the places the picture colours; a fin thickened by a tenth of a millimetre is drawn the same way.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - walls: _remedy_walls (a thin wall is offset outwards, half on each side)
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', 'kit'))
from mfgkit import *

t = Table("h8_colours_of_added_and_cut: one part that needs a gusset under a ledge (material added) and a teardrop over a bore (material cut), with a constraint of its own for each: the result is drawn as ONE body coloured by the change -- the part's own colour, red where it was added, blue where it was cut -- and the change map says +1, -1 and 0 at the places the picture colours; a fin thickened by a tenth of a millimetre is drawn the same way")

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
ledge = union(box((0, 0, 0), (10, 30, 30)), box((10, 0, 15), (26, 30, 19)))
block = difference(box((46, 0, 0), (86, 30, 30)), rotate_x(cylinder(5.0, 40, (66, 15, -35)), math.pi / 2))
part = union(ledge, block)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
fill = manufacturing_constraint(overhang=45, overhang_fix='fill', region=box((-5, -5, -5), (36, 35, 36)))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
cut = manufacturing_constraint(overhang=45, overhang_fix='cut', region=box((36, -5, -5), (96, 35, 36)))
lo, hi = (-3, -3, -3), (89, 33, 33)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[fill, cut], max_support_height=12, max_change=0.5)
print(res)
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('both kinds of change were made: material added (%.0f mm3) and cut (%.0f mm3)' % (res.added, res.removed), res.added > 800 and res.removed > 80, '+%.0f -%.0f' % (res.added, res.removed), '+ > 800, - > 80')
cm = res.change_map
gusset = (18.0, 15.0, 7.0)                                                            # (on the face of the gusset: 8 mm from the tip of the ledge, 8 mm down)
apex = (66.0, 15.0, 15.0 + 5.0 * math.sqrt(2.0) - 0.02)                               # (just under the apex of the teardrop, inside what was the block)
wall = (66.0, 15.0, 30.0)                                                             # (the top of the block: the part's own surface, nothing changed there)
face = (0.0, 15.0, 22.0)
values = evaluate(cm, [gusset, apex, wall, face])
t.check('the change map is +1 on the gusset (red), -1 under the apex of the teardrop (blue), and 0 on the faces nothing changed (grey)', values[0] >= 0.99 and values[1] <= -0.99 and abs(values[2]) < 0.01 and abs(values[3]) < 0.01,
        [round(v, 3) for v in values], '+1, -1, 0, 0')
shown = res._display()
t.check('the result is drawn as ONE body, coloured by the change (the part\'s own colour where it was as it was, red where material was added, blue where it was cut)', not isinstance(shown, list) and getattr(shown, '_color_field_name', None) == 'change', type(shown).__name__, 'one body coloured by the change')
fin = box((0, 0, 0), (0.6, 12, 10))
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
thin = manufacturing_optimization(fin, constraints=[manufacturing_constraint(min_wall=0.8)], max_change=3.0)
one = thin._display()
t.check('a fin thickened by 0.1 mm on each side is drawn as ONE body too (a layer thinner than a cell is not a body of stripes)', not isinstance(one, list), type(one).__name__, 'one body')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
