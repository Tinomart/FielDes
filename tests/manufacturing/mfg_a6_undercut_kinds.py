# Six blocks of 16 mm with a side groove, a blind pocket, a hole along z, a tunnel across z, a closed cavity and an overhanging ledge, for no undercut
# along z: the check must mark the groove, the tunnel and the cavity and the others not.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - undercut: _remedy_undercut (what lies under and over the part along the direction is filled)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('a6_undercut_kinds: six blocks of 16 mm with a side groove, a blind pocket, a hole along z, a tunnel across z, a closed cavity and an overhanging ledge, for no undercut along z: the check must mark the groove, the tunnel and the cavity and the others not')

# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
parts = []
spot = lambda k: (22.0 * (k % 3), 22.0 * (k // 3))                        # (three to a row, 22 mm apart, the rows 22 apart)
for k in range(6):
    cx, cy = spot(k)
    cube = box((cx, cy, 0), (cx + 16, cy + 16, 16))
    if k == 0:
        cube = difference(cube, box((cx - 1, cy + 4, 6), (cx + 8, cy + 12, 10)))
    elif k == 1:
        cube = difference(cube, box((cx + 4, cy + 4, 6), (cx + 12, cy + 12, 17)))
    elif k == 2:
        cube = difference(cube, cylinder(3.0, 18, (cx + 8, cy + 8, -1)))
    elif k == 3:
        cube = difference(cube, box((cx - 1, cy + 5, 5), (cx + 17, cy + 11, 11)))
    elif k == 4:
        cube = difference(cube, box((cx + 5, cy + 5, 5), (cx + 11, cy + 11, 11)))
    else:
        cube = union(box((cx, cy, 0), (cx + 6, cy + 16, 16)), box((cx, cy, 10), (cx + 16, cy + 16, 16)))
    parts.append(cube)
part = parts[0]
for p in parts[1:]:
    part = union(part, p)
# WHAT IS ASKED: the rules the part has to keep (the only conditions the tool is given: it has no loads, supports or material -- it works on the shape alone)
rule = manufacturing_constraint(undercut_along=(0, 0, 1))
lo, hi = (-4, -4, -3), (22.0 * 2 + 20, 22.0 + 20, 19)
# (the view of the window: the box round the part, and how finely it is drawn)
frame(lo, hi, res=3)
report = manufacturing_analysis(part, constraints=[rule], samples=12000)
print(report)
# THE ANSWER BY HAND, from what an undercut is: a place a tool coming along z, from either side, cannot reach: a ray along z from it meets the part both ways
x = lambda k, dx, dy, z: (spot(k)[0] + dx, spot(k)[1] + dy, z)
probes = [('side groove: its ceiling', x(0, 3.0, 8.0, 10.0), True), ('side groove: its floor', x(0, 3.0, 8.0, 6.0), True),
          ('blind pocket: its floor', x(1, 8.0, 8.0, 6.0), False), ('hole along z: its wall', x(2, 8.0 + 2.9, 8.0, 8.0), False),
          ('tunnel across z: its ceiling', x(3, 8.0, 8.0, 11.0), True), ('closed cavity: its ceiling', x(4, 8.0, 8.0, 11.0), True),
          ('ledge: its underside', x(5, 11.0, 8.0, 10.0), False)]
for name, p, should in probes:
    m = margin_at(report, 'undercut_along', [p])[0]
    t.check('%s: %s by the check' % (name, 'marked' if should else 'left alone'), (m > 0) == should, '%.2f' % m, '> 0' if should else '<= 0')
n = len(report.report['undercut_along']['regions'])
# (a feature gives a region for each cluster of samples -- the groove's ceiling and its floor are two faces, two clusters --, so the count is not three: what is asked is WHERE the regions are, in which of the six blocks)
blocks = sorted(set(int(g['at'][0] // 22.0) + 3 * int(g['at'][1] // 22.0) for g in report.report['undercut_along']['regions']))
# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
t.check('the regions of the report are in the blocks of the groove, the tunnel and the cavity and in no other (blocks %s of 0 .. 5; %d regions)' % (blocks, n), blocks == [0, 3, 4], blocks, [0, 3, 4])

t.summary()
result = report
original = aside(part, lo, hi)[0]
result
original
