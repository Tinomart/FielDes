# A small ledge on a wall (10 x 10 x 12) with a block of support under it, written as a print: the 3MF holds the part, the supports and the no-support
# volume as three objects of the right volumes, the DXF has one layer for each millimetre of height, the SVG slice has the area of the section -- the
# one case of the suite that writes files, because writing them is a feature of the product.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - overhang: _remedy_overhang (a gusset under it, or a teardrop cut above it)
#                      - supports: _supports_field (a block under what is left leaning out)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'examples', 'manufacturing', 'kit'))
from mfgkit import *

t = Table('s4_print_files: a small ledge on a wall (10 x 10 x 12) with a block of support under it, written as a print: the 3MF holds the part, the supports and the no-support volume as three objects of the right volumes, the DXF has one layer for each millimetre of height, the SVG slice has the area of the section -- the one case of the suite that writes files, because writing them is a feature of the product')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
part = union(box((0, 0, 0), (4, 10, 12)), box((4, 0, 6), (10, 10, 8)))
strip = box((8, -1, -1), (11, 11, 13))
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(overhang=45, support='block', support_gap=0.3, no_support=[strip])
# THE TOOL RUNS HERE: it reads the part's field and makes the remedies (see the list in the header); nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=[rule], max_support_height=6, max_change=0.02)
lo, hi = (-2, -2, -2), (12, 12, 14)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=6)
print(res)
pts = points_in((3, -1, -1), (9, 11, 7), 120000, seed=9)
inside = [p for p, v in zip(pts, evaluate(res.supports, pts)) if v <= 0]
volume = (6 * 12 * 8) * len(inside) / 120000.0
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('there is a block of support under the ledge (%.0f mm3 by the field)' % volume, volume > 30, '%.0f mm3' % volume, '> 30')
d = scratch('print_files')
p3 = export_print(res, os.path.join(d, 'print.3mf'))
objs = read_3mf(p3)
kinds = sorted(o['type'] for o in objs)
t.check('3MF: three objects, the part, the supports and the no-support volume', kinds == ['model', 'other', 'support'], kinds, "['model', 'other', 'support']")
t.check('3MF: every object has triangles and encloses volume', all(o['triangles'] > 0 and o['volume'] > 0 for o in objs), [(o['type'], o['triangles'], round(o['volume'])) for o in objs], 'all positive')
sup = [o for o in objs if o['type'] == 'support']
if sup:
    t.near('3MF: the support object encloses what the field says', sup[0]['volume'], volume, 0.15 * volume + 5.0, ' mm3')
mod = [o for o in objs if o['type'] == 'model']
if mod:
    t.near('3MF: the part encloses its own volume (4 x 10 x 12 + 6 x 10 x 2 = 600)', mod[0]['volume'], 600.0, 40.0, ' mm3')
dx = export_layers(res, os.path.join(d, 'layers.dxf'), layer_height=1.0)
layers, entities = read_dxf_layers(dx)
t.check('DXF: 12 layers for a part 12 high at 1 mm', len(layers) == 12, len(layers), 12)
t.check('DXF: the layer names are L0001 .. L0012', sorted(layers)[0] == 'L0001' and sorted(layers)[-1] == 'L0012', (sorted(layers)[:1], sorted(layers)[-1:]), 'L0001 .. L0012')
sv = export_slice(res, os.path.join(d, 'slice.svg'), 10.0)
area, paths = svg_area(sv)
t.near('SVG: the layer at z = 10 is the wall alone, 4 x 10 = 40 mm2', area, 40.0, 2.0, ' mm2')

t.summary()
result = res
original = aside(part, lo, hi)[0]
result
original
