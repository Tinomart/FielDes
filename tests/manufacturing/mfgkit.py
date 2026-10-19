# mfgkit.py: what the manufacturing cases share, as functions: the frame of the view, the checks every case makes on a result (generic_checks, thin_wall_checks, arc_checks, control,
# complicated_checks), the generator of parts the code has never seen (random_part, random_constraint, sweep_one, run_sweep).  The cases themselves are NOT here: every
# mfg_tNN_<name>.py file holds its own part, constraint, tool call, answers by hand and checks, to be read in the editor.  The mechanics under these (the tape measure, reading
# a field at many points, reading back the DXF layers and the SVG slice that a print writes) are in mfgtest.py.  No test exports a mesh.
import math
import os

from mfgtest import *







def _shift(lo, hi):
    ''' how far to the right the copy of the part as it was is moved: its own width and a third more '''
    return 1.35 * (hi[0] - lo[0])


def frame(lo, hi, res=4, quality=8):
    ''' The view: the box of the part, and room to its right for the copy of the part as it was (see aside) '''
    pad = 0.08 * max(hi[i] - lo[i] for i in range(3))
    view.set_bounds([lo[0] - pad, lo[1] - pad, lo[2] - pad], [hi[0] + _shift(lo, hi) + pad, hi[1] + pad, hi[2] + pad], auto_scale=True)
    view.set_resolution(res)
    view.set_quality(quality)


def aside(part, lo, hi):
    ''' The part as it was, moved to the right of its own box, to be shown beside what was made of it. Returns (the copy, the distance moved) '''
    dx = _shift(lo, hi)
    return move(part, (dx, 0, 0)), dx


def thin_wall_checks(t, part, res, constraints, lo, hi, limit, tapes, expect_added=None, tol_added=None, face=None, reach=None, name='part'):
    ''' The checks that every wall case shares.
          limit        the thinnest wall that was asked for (mm)
          tapes        [(point inside the wall, direction across it)]: the wall measured there with a tape measure through the result: limit .. limit + 6 %
          expect_added the material that should have been added, by hand (mm^3), tol_added: how far off the measurement may be
          face         ((x0, y0, z0), (x1, y1, z1)): the box the added material has to stay in -- the face of the thin wall and nothing past its edges
          reach        nothing may be added farther from the part than this (mm) '''
    added, removed, cells = changed_volumes(part, res, lo, hi)
    if expect_added is not None:
        t.near('%s: material added (by hand %.4g mm3)' % (name, expect_added), added, expect_added, tol_added if tol_added is not None else 0.06 * expect_added + 2.0, ' mm3')
        t.near('%s: the tool\'s own number agrees with the measurement' % name, res.added, added, 0.12 * max(added, 1.0) + 3.0, ' mm3')
    t.check('%s: nothing cut' % name, removed < 1.5, '%.3g mm3' % removed, '0')
    cmp_ = compare(part, res, lo, hi, n=200000)
    if face is not None:
        b = box_of([p for p, _ in cmp_['added']])
        ok = b is not None and all(b[0][i] >= face[0][i] - 0.12 and b[1][i] <= face[1][i] + 0.12 for i in range(3))
        t.check('%s: the added material stays inside the face of the wall (no lip, no fragment past its edges)' % name, ok, b, face)
    if reach is not None:
        bad = debris(cmp_, reach)
        t.check('%s: nothing added farther than %.3g mm from the part' % (name, reach), len(bad) == 0, '%d points' % len(bad), 0)
    for point, direction in tapes:
        behind, ahead = run_length(res, point, direction, 6.0, 6000)
        t.between('%s: wall measured at %s along %s' % (name, tuple(round(c, 2) for c in point), direction), behind + ahead, limit - 0.012, limit * 1.06 + 0.02, ' mm')
    before = manufacturing_analysis(part, constraints=constraints, samples=5000)
    after = manufacturing_analysis(res, constraints=constraints, samples=5000)
    s_before = sum(r['share'] for r in before.report.values())
    s_after = sum(r['share'] for r in after.report.values())
    if not after.compliant:
        print(after)                       # (what it found, and where)
    # (the faces are thickened and the tape measures above are the proof; what the check still marks are the places the wall remedy does not fill -- the notches along the edges where thin walls
    # meet and in the inside corners where one stands on a thick face (docs/manufacturing.md): at most a tenth of what it marked before, and under 5 % of the surface)
    t.check('%s: the independent check marked %.1f %% of the surface before and marks %.2f %% after (the places left are the notches along edges and in inside corners, listed above): at most a tenth of it and under 5 %%' % (name, 100.0 * s_before, 100.0 * s_after),
            s_after <= 0.1 * s_before and s_after <= 0.05, '%.2f %% (was %.1f %%)' % (100.0 * s_after, 100.0 * s_before), '<= a tenth, < 5 %')


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# Walls (min_wall): a thin wall is thickened to the limit over its whole face, up to its edges and not past them










# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# What every other case shares

def generic_checks(t, part, res, constraints, lo, hi, added=None, removed=None, tol=None, reach=None, within=None, name='part', keep_rules=True, skin=0.0):
    ''' Checks on a result, each measured on the field and not on what the tool says of itself:
          added / removed   the material that should have been added / cut, by hand (mm^3); None for removed means that nothing may be cut
          reach             nothing may be added farther from the part than this (mm)
          within            ((x0, y0, z0), (x1, y1, z1)): everything added or cut lies in this box
          skin              a change within this distance of the part's surface (mm) is not counted for `within`: the library's fillet is the distance to a mesh, good to about 0.03 mm
          keep_rules        the independent check (manufacturing_analysis) of the result finds no broken rule '''
    a, r, cells = changed_volumes(part, res, lo, hi)
    if added == 'any':
        t.check('%s: the material added (no hand answer for this part): %.4g mm3' % (name, a), True)
    elif added is not None:
        t.near('%s: material added (by hand %.4g mm3)' % (name, added), a, added, tol if tol is not None else 0.08 * added + 2.0, ' mm3')
    else:
        t.check('%s: nothing added' % name, a < 1.5, '%.3g mm3' % a, '0')
    if removed == 'any':
        t.check('%s: the material cut (no hand answer for this part): %.4g mm3' % (name, r), True)
    elif removed is not None:
        t.near('%s: material cut (by hand %.4g mm3)' % (name, removed), r, removed, tol if tol is not None else 0.08 * removed + 2.0, ' mm3')
    else:
        t.check('%s: nothing cut' % name, r < 1.5, '%.3g mm3' % r, '0')
    t.near('%s: the tool\'s own count of what it added agrees' % name, res.added, a, 0.12 * max(a, 1.0) + 3.0, ' mm3')
    cmp_ = compare(part, res, lo, hi, n=200000)
    frags = floating_clusters(cmp_)
    t.check('%s: no piece of added material floats beside the part' % name, len(frags) == 0, '%d piece(s)' % len(frags), 0)
    if reach is not None:
        bad = debris(cmp_, reach)
        t.check('%s: nothing added farther than %.3g mm from the part' % (name, reach), len(bad) == 0, '%d points' % len(bad), 0)
    if within is not None:
        moved = [p for p, x in cmp_['added'] if abs(x) > skin] + [p for p, x in cmp_['removed'] if abs(x) > skin]
        b = box_of(moved)
        ok = b is None or all(b[0][i] >= within[0][i] - 0.15 and b[1][i] <= within[1][i] + 0.15 for i in range(3))
        t.check('%s: every change lies inside its box' % name, ok, b, within)
    if keep_rules:
        after = manufacturing_analysis(res, constraints=constraints, samples=5000)
        if not after.compliant:
            print(after)                   # (what it found, and where)
        found = {k: v['share'] for k, v in after.report.items() if v['broken']}
        if keep_rules == 'listed':
            # (a part with a narrow slot, a thin fin, a boss and a hole at once, for a tool that cannot make everything right in one pass -- a fillet and a thickened wall that meet leave a small crease: the
            # tool says so in `fail_states`, which is what is asked of it.  What the independent check finds must be what the tool lists: each broken rule is listed, and its share is at most one and a
            # half times what the tool lists plus half a per cent of the surface, the edge residue of the other cases)
            listed = {}
            for s in res.fail_states:
                listed[s['rule']] = listed.get(s['rule'], 0.0) + s['share']
            # (`fail_states` lists the five biggest places of each rule: what the tool's re-check found in all is `fail_totals`, and that is what the independent check is compared with)
            if getattr(res, 'fail_totals', None):
                listed = {k: v['share'] for k, v in res.fail_totals.items()}
            ok = all(share <= 1.5 * listed.get(k, 0.0) + 0.005 for k, share in found.items())
            t.check('%s: what the independent check still finds is what the tool lists as broken (tool %s; check %s)' % (name, {k: round(v, 4) for k, v in listed.items()} or 'nothing', {k: round(v, 4) for k, v in found.items()} or 'nothing'),
                    ok, {k: round(v, 4) for k, v in found.items()}, 'each rule at most 1.5 x the tool\'s share + 0.5 %')
        else:
            t.check('%s: the independent check of the result finds no broken rule' % name, after.compliant, {k: round(v, 4) for k, v in found.items()}, 'none')


def residue_check(t, name, part, res, constraints, samples=5000, what='the places left are the edge residue listed above', exact=False, share=0.25, limit=0.005):
    ''' The independent check of a result, as a before and an after: what it marked of the part's surface before the shell and what it marks after.  A face is made right, and the tapes and the
        volumes of the case say so; what the check still marks are samples at creases and at the edges of what was added -- a strip of a few tenths of a millimetre where a gusset or a fillet
        ends on the part's own face (docs/manufacturing.md): at most a quarter of what it marked before and under 0.5 % of the surface.  The check is run on the part as well, so that a case
        whose part had nothing to mark is held to none. '''
    if exact:
        # (the corner rule reads the VALUE of the field up to the ball's radius from the surface, which is a distance only where the field is one: a part made by differences of boxes, whose cutters
        # overshoot the block, has inside its slots the distance to the cutter's faces too -- 2.24 where the part's own is 3.0 -- and the ball is measured as not fitting along every wall under a mouth.
        # docs/manufacturing.md says to read such a part through `exact_distance`; the check is run on that)
        part, res = exact_distance(part), exact_distance(res)
    before = manufacturing_analysis(part, constraints=constraints, samples=samples)
    after = manufacturing_analysis(res, constraints=constraints, samples=samples)
    s_before = sum(r['share'] for r in before.report.values())
    s_after = sum(r['share'] for r in after.report.values())
    if not after.compliant:
        print(after)                       # (what it found, and where)
    # (a half per cent is 25 points of 5000: the edges where something was added, a few samples each; the same number as `spare` of the job tables)
    ok = (s_after == 0.0) if s_before == 0.0 else (s_after <= share * s_before and s_after <= limit)
    t.check('%s: the independent check marked %.2f %% of the surface before and marks %.2f %% after (%s): at most %.0f %% of it and under %.1f %%' % (name, 100.0 * s_before, 100.0 * s_after, what, 100.0 * share, 100.0 * limit),
            ok, '%.3f %% (was %.2f %%)' % (100.0 * s_after, 100.0 * s_before), '<= %.0f %% of it, < %.1f %%' % (100.0 * share, 100.0 * limit))


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# Gaps (min_gap): a gap that is too narrow is closed, or made as wide as asked





# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# Inside corners (min_radius): filled by the arc of a ball that rolls in them

def arc_checks(t, res, centre, radius, a0=10, a1=80, step=10, z=10.0, towards=(-1, -1), name='corner'):
    ''' The fillet of an inside corner is the arc of a ball of the radius that rolls in it.  Seen from the centre of that ball: a point 0.2 mm farther than the radius, toward the
        corner, is material; a point 0.2 mm nearer than the radius is air.  Checked every `step` degrees of the arc, in the plane z. '''
    bad_in, bad_out = [], []
    for deg in range(a0, a1 + 1, step):
        a = math.radians(deg)
        d = (towards[0] * math.cos(a), towards[1] * math.sin(a))
        mat = (centre[0] + d[0] * (radius + 0.2), centre[1] + d[1] * (radius + 0.2), z)
        air = (centre[0] + d[0] * (radius - 0.2), centre[1] + d[1] * (radius - 0.2), z)
        vm, va = evaluate(res, [mat, air])
        if vm > 0:
            bad_in.append(deg)
        if va <= 0:
            bad_out.append(deg)
    t.check('%s: material 0.2 mm outside the arc of radius %g at every %d degrees (%d..%d)' % (name, radius, step, a0, a1), not bad_in, 'air at %s degrees' % bad_in, 'none')
    t.check('%s: air 0.2 mm inside the arc of radius %g at every %d degrees' % (name, radius, step), not bad_out, 'material at %s degrees' % bad_out, 'none')






# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# Draft and undercut





# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# Overhang: a gusset under a ledge, a teardrop above a hole, a bridge that is only reported







# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# Controls: smooth parts that keep every rule must come out untouched and unreported

def control(t, part, lo, hi, name, res_=3, rules=None):
    rule = rules or manufacturing_constraint(min_wall=0.8, min_gap=0.4, min_radius=1.0, max_bridge=20.0)
    res = manufacturing_optimization(part, constraints=[rule], max_change=0.5, verify=True)
    frame(lo, hi, res=res_)
    a, r, _ = changed_volumes(part, res, lo, hi)
    t.check('%s: nothing added' % name, a < 1.5, '%.3g mm3' % a, 0)
    t.check('%s: nothing cut' % name, r < 1.5, '%.3g mm3' % r, 0)
    t.check('%s: the tool lists nothing as broken' % name, not res.fail_states, [s['rule'] for s in res.fail_states], 'none')
    after = manufacturing_analysis(part, constraints=[rule], samples=6000)
    t.check('%s: the independent check of the part finds no broken rule' % name, after.compliant, {k: round(v['share'], 4) for k, v in after.report.items() if v['broken']}, 'none')
    return res












# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# The limits of the shell: the budget, a region, a kept region, a second pass









# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# Supports, the files a printer takes, the way up of a print







# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# The three printers' presets on one part with a bit of everything

def sampler():
    ''' A plate 70 x 40 x 5 with a thin fin, two blocks with a narrow slot between them, an L of two walls, a round boss and a hole: a bit of everything a process asks about '''
    plate = difference(box((0, 0, 0), (70, 40, 5)), cylinder(2.5, 7, (62, 8, -1)))
    fin = box((5, 5, 5), (5.5, 20, 17))
    block_a = box((10, 5, 5), (18, 20, 13))
    block_b = box((18.3, 5, 5), (26.3, 20, 13))
    ell = union(box((32, 5, 5), (44, 8, 15)), box((32, 5, 5), (35, 25, 15)))
    boss = cylinder(5, 10, (55, 28, 5))
    part = plate
    for piece in (fin, block_a, block_b, ell, boss):
        part = union(part, piece)
    return part


def preset_case(t, constraint, name):
    part = sampler()
    res = manufacturing_optimization(part, constraints=[constraint], max_change=1.0)
    lo, hi = (-3, -3, -3), (73, 43, 22)
    frame(lo, hi, res=2)
    print(res)
    generic_checks(t, part, res, [constraint], lo, hi, added='any', removed='any', name=name)
    return res, aside(part, lo, hi)[0]








# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# Parts the code has never seen: generated at random from a grammar, with random constraints.  The criteria are fixed BEFORE any of them is run (the same for every part):
#   1. the tool runs without an error
#   2. no piece of added material floats beside the part (every cluster of added points touches it)
#   3. everything changed lies inside the box of the part (and its margin)
#   4. for every rule asked whose remedy the tool made: the independent check of the result finds at most 1 % of the surface still breaking it, or the tool itself lists it as broken
#   5. the tool's own count of what it added and cut is within 12 % + 3 mm3 of the measured one
#   6. nothing is cut when no remedy that cuts was asked (a widened gap or a teardrop)
#   (no mesh is exported by any of the cases: what a case judges is the field, the independent check and the picture of the window)

def random_part(rng):
    ''' A plate with two to four features.  Returns (part, description, (width, depth, height)) '''
    W, D, tp = rng.uniform(35, 60), rng.uniform(30, 50), rng.uniform(2.5, 6.0)
    part = box((0, 0, 0), (W, D, tp))
    desc = ['plate %.0f x %.0f x %.1f' % (W, D, tp)]
    top = tp
    for _ in range(rng.randint(2, 4)):
        kind = rng.choice(['fin', 'fin', 'post', 'block', 'slot', 'slot', 'ledge', 'ell', 'hole'])
        x, y = rng.uniform(4, W - 16), rng.uniform(4, D - 16)
        if kind == 'fin':
            th, ln, h = rng.choice([0.3, 0.45, 0.6, 0.75, 1.0, 1.5]), rng.uniform(8, 20), rng.uniform(8, 22)
            piece = box((x, y, tp), (x + th, y + ln, tp + h)) if rng.random() < 0.5 else box((x, y, tp), (x + ln, y + th, tp + h))
            desc.append('fin %.2f' % th)
        elif kind == 'post':
            r, h = rng.uniform(1.0, 5.0), rng.uniform(6, 20)
            piece = cylinder(r, h, (x + 5, y + 5, tp))
            desc.append('post r%.1f' % r)
        elif kind == 'block':
            w, d, h = rng.uniform(4, 12), rng.uniform(4, 12), rng.uniform(5, 18)
            piece = box((x, y, tp), (x + w, y + d, tp + h))
            desc.append('block')
        elif kind == 'slot':
            gap, w, d, h = rng.choice([0.15, 0.25, 0.4, 0.8, 1.5, 2.5]), rng.uniform(5, 9), rng.uniform(8, 14), rng.uniform(6, 14)
            piece = union(box((x, y, tp), (x + w, y + d, tp + h)), box((x + w + gap, y, tp), (x + 2 * w + gap, y + d, tp + h)))
            desc.append('slot %.2f' % gap)
        elif kind == 'ledge':
            wt, h, ln = rng.uniform(3, 6), rng.uniform(12, 22), rng.uniform(6, 14)
            piece = union(box((x, y, tp), (x + wt, y + 14, tp + h)), box((x + wt, y, tp + h - 3), (x + wt + ln, y + 14, tp + h)))
            desc.append('ledge')
        elif kind == 'ell':
            th, ln, h = rng.uniform(2, 4), rng.uniform(10, 16), rng.uniform(8, 16)
            piece = union(box((x, y, tp), (x + ln, y + th, tp + h)), box((x, y, tp), (x + th, y + ln, tp + h)))
            desc.append('L')
        else:
            r = rng.uniform(0.5, 4.0)
            part = difference(part, cylinder(r, tp + 2, (x + 6, y + 6, -1)))
            desc.append('hole r%.1f' % r)
            continue
        part = union(part, piece)
        top = max(top, tp + 25)
    return part, ', '.join(desc), (W, D, top)


def random_constraint(rng):
    ''' One constraint: a preset or a handful of rules.  Returns (constraint, description) '''
    kind = rng.choice(['fff', 'cnc', 'molding', 'custom', 'custom', 'custom'])
    if kind == 'fff':
        n = rng.choice([0.4, 0.6])
        return fff_constraint(nozzle=n), 'fff nozzle %.1f' % n
    if kind == 'cnc':
        d = rng.choice([3.0, 4.0, 6.0])
        return cnc_constraint(tool_diameter=d), 'cnc tool %.0f' % d
    if kind == 'molding':
        w, g = rng.choice([1.0, 1.5, 2.0]), rng.choice([1.0, 2.0, 3.0])
        return molding_constraint(min_wall=w, draft=g), 'molding wall %.1f draft %.0f' % (w, g)
    rules = {}
    for name in rng.sample(['min_wall', 'min_gap', 'min_radius', 'draft', 'overhang'], rng.randint(1, 3)):
        rules[name] = {'min_wall': rng.choice([0.8, 1.2, 2.0]), 'min_gap': rng.choice([0.5, 1.0, 2.0]), 'min_radius': rng.choice([1.0, 2.0, 3.0]),
                       'draft': rng.choice([1.5, 2.5, 4.0]), 'overhang': rng.choice([45, 55])}[name]
    return manufacturing_constraint(**rules), 'custom ' + ', '.join('%s=%g' % kv for kv in sorted(rules.items()))


_RULE_OF = {'walls': 'min_wall', 'gaps': 'min_gap', 'corners': 'min_radius', 'undercut': 'undercut_along', 'draft': 'draft', 'overhang': 'overhang'}


def sweep_one(t, number, seed, offset=(0.0, 0.0)):
    ''' One generated part and constraint, run and checked by the seven criteria above.  The part stands at `offset` (x, y) so that several can be shown side by side.
        Returns (the result, the part as it was) '''
    rng = random.Random(seed)
    part, desc, (W, D, H) = random_part(rng)
    constraint, cdesc = random_constraint(rng)
    name = 'part %d (seed %d)' % (number, seed)
    print('--- %s: %s | %s' % (name, desc, cdesc))
    ox, oy = offset
    part = move(part, (ox, oy, 0))
    lo, hi = (ox - 3, oy - 3, -3), (ox + W + 3, oy + D + 3, H + 3)
    started = time.time()
    try:
        res = manufacturing_optimization(part, constraints=[constraint], max_change=1.0, verify=True)
    except Exception as e:
        t.check('%s: the tool runs without an error' % name, False, '%s: %s' % (type(e).__name__, str(e)[:120]), 'no error')
        return None, part
    t.check('%s: the tool runs without an error (%.0f s)' % (name, time.time() - started), True)
    print(res)
    a, r, _ = changed_volumes(part, res, lo, hi, n_coarse=100000, n_dense=100000)
    cmp_ = compare(part, res, lo, hi, n=100000)
    frags = floating_clusters(cmp_)
    t.check('%s: no piece of added material floats beside the part' % name, not frags, '%d piece(s)' % len(frags), 0)
    moved = [p for p, _ in cmp_['added']] + [p for p, _ in cmp_['removed']]
    b = box_of(moved)
    t.check('%s: everything changed lies inside the part\'s box' % name, b is None or all(b[0][i] >= lo[i] and b[1][i] <= hi[i] for i in range(3)), b, (lo, hi))
    kept = [c for c in res.changes if c['kept']]
    after = manufacturing_analysis(res, constraints=[constraint], samples=4000)
    listed = {s['rule'] for s in res.fail_states}
    for c in kept:
        rule = _RULE_OF[c['remedy']]
        if rule in after.report:
            share = after.report[rule]['share']
            t.check('%s: after the %s remedy the independent check finds at most 1 %% of the surface breaking %s (or the tool lists it)' % (name, c['remedy'], rule),
                    share <= 0.01 or rule in listed, '%.3g of the surface' % share, '<= 0.01')
    if not after.compliant:
        print(after)
    t.near('%s: the tool\'s own count of what it added agrees' % name, res.added, a, 0.12 * max(a, 1.0) + 3.0, ' mm3')
    t.near('%s: and of what it cut' % name, res.removed, r, 0.12 * max(r, 1.0) + 3.0, ' mm3')
    cutting = any(c['kept'] and (c['remedy'] == 'overhang' and getattr(constraint, 'overhang_fix', 'fill') == 'cut' or (c['remedy'] == 'gaps' and getattr(constraint, 'gap_fix', 'fill') == 'widen')) for c in res.changes)
    if not cutting:
        t.check('%s: nothing cut (no remedy that cuts was asked)' % name, r < 0.01 * max(1.0, cmp_.get('vol_part', 1.0)) + 2.0, '%.3g mm3' % r, 0)
    return res, part


def run_sweep(first_seed, count=6, columns=3):
    ''' `count` generated parts, one after the other, each shown at its own place (the results first, then the parts as they were behind them).  Returns the list of results '''
    t = Table('sweep: %d parts the code has never seen (seeds %d to %d)' % (count, first_seed, first_seed + count - 1))
    results, originals = [], []
    view.set_bounds([-5, -5, -5], [columns * 75, 2 * 60 + 40, 50], auto_scale=True)
    view.set_resolution(2)
    view.set_quality(8)
    for k in range(count):
        res, part = sweep_one(t, k + 1, first_seed + k, offset=((k % columns) * 75.0, (k // columns) * 60.0))
        results.append(res)
    t.summary()
    return results


# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# Parts that are really complicated: nobody can say by eye which strut is too thin or which face leans too far

def _step_file(name):
    ''' The STEP file of the examples, found from the folder of this file (a copy of it next to the case, or the examples of the repository) '''
    here = os.path.dirname(os.path.abspath(__file__))
    for rel in ('step', os.path.join('..', '..', 'examples', 'step'), os.path.join('..', 'examples', 'step')):
        p = os.path.join(here, rel, name)
        if os.path.exists(p):
            return p
    raise IOError('the STEP file %s was not found next to the case or in the examples' % name)


def complicated_checks(t, part, res, constraint, lo, hi, name, n=30000):
    ''' What can be said of a result with no answer by hand, with a heavy field (few points): that it floats nothing, that the tool's counts are what is measured, what the independent check
        says.  The pictures are what judges the rest. '''
    a, r, cells = changed_volumes(part, res, lo, hi, n_coarse=n, n_dense=n, cell=max(1.5, 0.02 * max(hi[i] - lo[i] for i in range(3))))
    print('%s: measured +%.4g -%.4g mm3, the tool says +%.4g -%.4g mm3' % (name, a, r, res.added, res.removed))
    t.near('%s: the tool\'s count of what it added agrees with the measurement' % name, res.added, a, 0.15 * max(a, 1.0) + 0.02 * max(res.volume, 1.0), ' mm3')
    cmp_ = compare(part, res, lo, hi, n=n)
    frags = floating_clusters(cmp_, cell=max(1.5, 0.02 * max(hi[i] - lo[i] for i in range(3))), touch=1.0)
    t.check('%s: no piece of added material floats beside the part' % name, not frags, '%d piece(s), the biggest %d points' % (len(frags), max([len(f) for f in frags], default=0)), 0)
    moved = [p for p, _ in cmp_['added']] + [p for p, _ in cmp_['removed']]
    b = box_of(moved)
    t.check('%s: everything changed lies inside the part\'s box' % name, b is None or all(b[0][i] >= lo[i] and b[1][i] <= hi[i] for i in range(3)), b, (lo, hi))
    after = manufacturing_analysis(res, constraints=[constraint], samples=3000)
    print(after)
    broken = {k: round(v['share'], 4) for k, v in after.report.items() if v['broken']}
    before = manufacturing_analysis(part, constraints=[constraint], samples=3000)
    for rule, rb in before.report.items():
        ra = after.report[rule]
        t.check('%s: the share of the surface breaking %s did not grow (%.3g before, %.3g after)' % (name, rule, rb['share'], ra['share']), ra['share'] <= rb['share'] + 0.002, '%.4g' % ra['share'], '<= %.4g' % (rb['share'] + 0.002))
    t.check('%s: the tool\'s list of what is still broken agrees with the independent check (%s)' % (name, broken), (not broken) or bool(res.fail_states) or res.after is None, '%d listed%s' % (len(res.fail_states), '' if res.after is not None else ' (the tool did not re-check: verify=False)'), 'something listed, or not asked')
    return after






# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# QUESTIONS, not repeats.  Each of the following asks the tool something it could answer wrongly: does it care which way a wall faces, how curved it is, where it stands, how big it is,
# how many holes there are, whether the part is turned or mirrored?  A family of variants is one file, one table, one picture of all of them side by side.

def rot_y(v, degrees):
    ''' the vector v turned by `degrees` about the y axis (right-handed) '''
    a = math.radians(degrees)
    return (v[0] * math.cos(a) + v[2] * math.sin(a), v[1], -v[0] * math.sin(a) + v[2] * math.cos(a))


def rot_z(v, degrees):
    ''' the vector v turned by `degrees` about the z axis (right-handed) '''
    a = math.radians(degrees)
    return (v[0] * math.cos(a) - v[1] * math.sin(a), v[0] * math.sin(a) + v[1] * math.cos(a), v[2])


def normal_of(field, p, h=0.01):
    ''' the unit normal of a body's surface at the point p on it: the gradient of the field there, by central differences (no convention of any rotation is trusted) '''
    pts = []
    for d in ((h, 0, 0), (-h, 0, 0), (0, h, 0), (0, -h, 0), (0, 0, h), (0, 0, -h)):
        pts.append((p[0] + d[0], p[1] + d[1], p[2] + d[2]))
    v = evaluate(field, pts)
    g = ((v[0] - v[1]) / (2 * h), (v[2] - v[3]) / (2 * h), (v[4] - v[5]) / (2 * h))
    m = math.sqrt(sum(c * c for c in g)) or 1.0
    return tuple(c / m for c in g)


def wall_through(field, outer_point, outer_shape, limit=3.0):
    ''' How thick `field` is through the wall that `outer_shape` has at the point `outer_point` of its outer surface: a tape measure from a quarter of a millimetre inside that point, along the
        normal, through `field`.  Returns the thickness (mm) and the normal '''
    n = normal_of(outer_shape, outer_point)
    start = (outer_point[0] - 0.2 * n[0], outer_point[1] - 0.2 * n[1], outer_point[2] - 0.2 * n[2])
    behind, ahead = run_length(field, start, n, limit, 6000)
    return behind + ahead, n




def curved_shells(t, defs, shifts, bounds):
    ''' Hollow bodies with a wall 0.5 mm thick (the outer shape minus the inner one), each at its own place of the picture, a wall rule of 0.8 mm asked: the wall measured along its normal at
        a point of the outer surface before and after, nothing cut, nothing floating, and the independent check.  The view is fine enough that the wall is 4 cells thick (res 8) '''
    rule = manufacturing_constraint(min_wall=0.8)
    view.set_bounds(bounds[0], bounds[1], auto_scale=True)
    view.set_resolution(8)
    view.set_quality(8)
    results = []
    for (name, outer, inner, point, big, *rest), (sx, sy) in zip(defs, shifts):
        rim_ok = rest[0] if rest else None            # (a shell with an open rim or a tip: a function of the height z that says where they are)
        part = move(difference(outer, inner), (sx, sy, 0))
        outer_s = move(outer, (sx, sy, 0))
        p = (point[0] + sx, point[1] + sy, point[2])
        lo, hi = (sx - big, sy - big, -3), (sx + big, sy + big, 2 * big + 3)
        before_t, n = wall_through(part, p, outer_s)
        t.near('%s: the wall as it is, measured along its normal (built 0.5 thick)' % name, before_t, 0.5, 0.02, ' mm')
        res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
        results.append(res)
        after_t, _ = wall_through(res, p, outer_s)
        t.between('%s: the wall made, measured the same way (0.8 asked)' % name, after_t, 0.78, 0.87, ' mm')
        cmp_ = compare(part, res, lo, hi, n=80000)
        t.check('%s: no piece of added material floats beside the wall' % name, not floating_clusters(cmp_), '', 0)
        t.check('%s: nothing cut' % name, len(cmp_['removed']) == 0, '%d points' % len(cmp_['removed']), 0)
        after = manufacturing_analysis(res, constraints=[rule], samples=3000)
        if not after.compliant:
            print(after)
        regions = after.report['min_wall']['regions']
        # (an open rim and a tip are EDGES: the faces of the wall are thickened up to them and not past them, so a notch of the growth is left along them -- the documented limit of docs/manufacturing.md.
        # A cone 25 mm high with a base of radius 12 has a rim of 75 mm and the check marks 2.2 % of its surface there.  The notch itself is under 0.5 mm high -- the wall of the result is 0.800 from 0.55 mm over the rim up, 0.69 at 0.3 and 0.57 at 0.15 --
        # and the regions the check lists are the means of the places it marks that lie less than two lengths of the rule apart, so each lies within one wall thickness of the base plane (0.8 mm) or at the tip)
        on_rim = bool(regions) and rim_ok is not None and all(rim_ok(g['at'][2]) for g in regions)
        t.check('%s: the independent check finds at most 2 %% of the surface still thin%s' % (name, ', or every place it lists is on the rim or at the tip (the edge notch of docs/manufacturing.md)' if rim_ok else ''),
                after.report['min_wall']['share'] <= 0.02 or on_rim, '%.3g%s' % (after.report['min_wall']['share'], (' (regions at z %s)' % ', '.join('%.2f' % g['at'][2] for g in regions)) if regions else ''), '<= 0.02' + (' or all on the rim / the tip' if rim_ok else ''))
    return results, None














# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# A. The check (manufacturing_analysis): does it find what is broken, and only that.  (TEST_DESIGN.md, group A)  The answer of each case is worked out from the DEFINITION of the rule
# and the grace of 5 % (GRACE), never from a constant of the code.

def margin_at(report, rule, points):
    ''' The check's own field for `rule` at points: above 0 where the rule is broken there (by how much, in the unit of the rule) -- the colour the picture shows '''
    return evaluate(report.fields[rule], points)


def region_boxes(report, rule):
    ''' The boxes of the regions the report lists for a rule: [((x0, y0, z0), (x1, y1, z1))] '''
    return [g['box'] for g in report.report[rule]['regions']]








def rounded_pocket(x0, y0, w, d, r, z0, z1):
    ''' The cutter of a pocket: a rectangle w x d with its four corners rounded to radius r (r = 0: sharp corners), from z0 to z1 '''
    if r <= 0:
        return box((x0, y0, z0), (x0 + w, y0 + d, z1))
    # (ONE convex primitive, so that the field inside it -- the air of the pocket -- is the distance to its wall: a union of two boxes and four cylinders has inside it the deepest of their own
    # depths, 1.4 mm 3 mm from a wall, and the corner rule, which reads the field up to the ball's radius, took the middle of every wall of such a pocket for a corner)
    return extrude(rounded_rectangle((x0, y0), (x0 + w, y0 + d), r), z0, z1)






def leaning_faces(part, cx, g, oy=0.0, length=4.0):
    ''' The two big faces of a plate that leans g degrees off the vertical (the plate was turned about the y axis through (cx, oy + 6, 0), one way or the other, and lifted 1.2 mm): a point of each,
        `length` along the plate from its foot, found with a tape measure through the part along the plate's thickness.  Returns (the point of one face, the point of the other) '''
    th = math.radians(g)
    for s in (1.0, -1.0):
        phi = s * th
        c = (cx + length * math.sin(phi), oy + 6.0, length * math.cos(phi) + 1.2)
        if evaluate(part, [c])[0] <= 0:
            d = (math.cos(phi), 0.0, -math.sin(phi))
            behind, ahead = run_length(part, c, d, 3.0, 6000)
            return ((c[0] + ahead * d[0], c[1], c[2] + ahead * d[2]), (c[0] - behind * d[0], c[1], c[2] - behind * d[2]))
    raise AssertionError('the leaning plate was not found at its place')




def added_in(cmp_, lo, hi):
    ''' The points of `compare` where material was added that lie in the box (lo, hi), and the volume they stand for (mm^3) '''
    pts = [p for p, _ in cmp_['added'] if all(lo[i] <= p[i] <= hi[i] for i in range(3))]
    return pts, len(pts) * cmp_['box'] / float(cmp_['n'])






















































# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# The rest of the design (TEST_DESIGN.md): inside corners of three faces and of all sizes, a slot the ball cannot enter, arches and cavities, the thresholds of the overhang remedy, supports,
# the way up found over directions, the shell (which remedy runs, what cannot be fixed, the colours), and every refusal























# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# THE JUDGE OF THE JOB TESTS (JOB_TESTS.md).  Measurements of a finished part made with routines and numbers of THIS file: not the library's check, and not its constants.  What both the shell and
# `manufacturing_analysis` are blind to is what an outside look can still find.  The judge is itself tested first, on parts whose answer is known (a slab, a step, a sphere, a pocket).

def _judge_gradients(field, pts, h):
    ''' the gradient of `field` at each point by central differences of `h`: one evaluation of the field at 6 places round every point '''
    probes = []
    for p in pts:
        probes.extend(((p[0] + h, p[1], p[2]), (p[0] - h, p[1], p[2]), (p[0], p[1] + h, p[2]), (p[0], p[1] - h, p[2]), (p[0], p[1], p[2] + h), (p[0], p[1], p[2] - h)))
    v = evaluate(field, probes)
    return [((v[6 * i] - v[6 * i + 1]) / (2 * h), (v[6 * i + 2] - v[6 * i + 3]) / (2 * h), (v[6 * i + 4] - v[6 * i + 5]) / (2 * h)) for i in range(len(pts))]


def _judge_unit(g):
    m = math.sqrt(g[0] ** 2 + g[1] ** 2 + g[2] ** 2) or 1.0
    return (g[0] / m, g[1] / m, g[2] / m)


def judge_surface_points(field, lo, hi, count, seed=1, band=1.5):
    ''' `count` points ON the surface of `field` (negative inside) in the box (lo, hi), and the unit normal at each, pointing out of the part.  Random points of the box are thrown, those within `band`
        mm of the surface are kept (read from the field), each is carried onto the surface by two steps of  p - f n  (n by central differences of 0.02 mm) and kept if it lands within 0.003 mm of
        it.  Returns (points, normals) '''
    rng = random.Random(seed)
    cloud = [tuple(rng.uniform(lo[i], hi[i]) for i in range(3)) for _ in range(count * 12)]
    near = [p for p, v in zip(cloud, evaluate(field, cloud)) if abs(v) <= band][:count * 2]
    h = 0.02
    for _ in range(2):
        vals = evaluate(field, near)
        grads = _judge_gradients(field, near, h)
        moved = []
        for p, v, g in zip(near, vals, grads):
            n = _judge_unit(g)
            moved.append((p[0] - v * n[0], p[1] - v * n[1], p[2] - v * n[2]))
        near = moved
    vals = evaluate(field, near)
    pts = [p for p, v in zip(near, vals) if abs(v) <= 0.003][:count]
    return pts, [_judge_unit(g) for g in _judge_gradients(field, pts, h)]


def judge_thickness(field, pts, normals, limit, inward=True, rays=9):
    ''' How thick the material is (inward) or how wide the air is (not inward) at each surface point, in mm: the shortest chord of `rays` lines from the point -- the normal, and a ring of eight
        leaning 30 degrees from it --, each followed in 16 steps to 1.5 x `limit` and ended where the field changes sign (found to a thousandth of the step by halving), counted as its length x the
        cosine of its lean (the distance between the two faces across the line) when the face it ends on faces back at the point (its normal within 45 degrees of opposite).  1.5 x `limit` where
        no line ends on such a face.  (A ring of 30 degrees and a facing of 45 degrees: not the numbers the library's check uses) '''
    cone = math.radians(30.0)
    sign = -1.0 if inward else 1.0
    steps = 16
    reach = 1.5 * limit
    dirs = []
    for n in normals:
        e1 = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
        e1 = _judge_unit((n[1] * e1[2] - n[2] * e1[1], n[2] * e1[0] - n[0] * e1[2], n[0] * e1[1] - n[1] * e1[0]))
        e2 = (n[1] * e1[2] - n[2] * e1[1], n[2] * e1[0] - n[0] * e1[2], n[0] * e1[1] - n[1] * e1[0])
        ring = [(sign * math.cos(cone) * n[a] + math.sin(cone) * (math.cos(2 * math.pi * j / 8.0) * e1[a] + math.sin(2 * math.pi * j / 8.0) * e2[a])) for j in range(8) for a in range(3)]
        dirs.append([(sign * n[0], sign * n[1], sign * n[2])] + [tuple(ring[3 * j: 3 * j + 3]) for j in range(8)][:rays - 1])
    start = [(p[0] - sign * 0.004 * n[0], p[1] - sign * 0.004 * n[1], p[2] - sign * 0.004 * n[2]) for p, n in zip(pts, normals)]
    flat = []
    for q, ds in zip(start, dirs):
        for d in ds:
            for k in range(1, steps + 1):
                s = reach * k / float(steps)
                flat.append((q[0] + s * d[0], q[1] + s * d[1], q[2] + s * d[2]))
    vals = evaluate(field, flat)
    flip = 1.0 if inward else -1.0
    hits = []                                    # (point index, ray, low s, high s)
    for i, ds in enumerate(dirs):
        for r in range(len(ds)):
            base = (i * len(ds) + r) * steps
            previous, s_prev = -0.001, 0.0
            for k in range(steps):
                b = flip * vals[base + k]
                s = reach * (k + 1) / float(steps)
                if previous < 0 <= b:
                    hits.append([i, r, s_prev, s])
                    break
                previous, s_prev = b, s
    for _ in range(10):                          # (halving: the field's sign at the middle decides which half keeps the crossing)
        mids = [(start[i][0] + 0.5 * (a + b) * dirs[i][r][0], start[i][1] + 0.5 * (a + b) * dirs[i][r][1], start[i][2] + 0.5 * (a + b) * dirs[i][r][2]) for i, r, a, b in hits]
        mv = evaluate(field, mids) if mids else []
        for h, v in zip(hits, mv):
            m = 0.5 * (h[2] + h[3])
            if flip * v < 0:
                h[2] = m
            else:
                h[3] = m
    ends = [(start[i][0] + h3 * dirs[i][r][0], start[i][1] + h3 * dirs[i][r][1], start[i][2] + h3 * dirs[i][r][2]) for i, r, h2, h3 in hits]
    grads = _judge_gradients(field, ends, 0.01) if ends else []
    out = [reach] * len(pts)
    for (i, r, h2, h3), g in zip(hits, grads):
        gn = _judge_unit(g)
        n = normals[i]
        if gn[0] * n[0] + gn[1] * n[1] + gn[2] * n[2] <= -math.cos(math.radians(45.0)):
            lean = math.cos(cone) if r > 0 else 1.0
            out[i] = min(out[i], h3 * lean)
    return out


def judge_overhang(pts, normals, up, limit, floor=None):
    ''' 1 for each surface point whose face looks DOWN and leans out more than `limit` degrees from the vertical (`up` the way up of the print), except the first 0.6 mm above the plate (`floor`:
        the lowest height of the part along `up`, found from the points when not given) '''
    un = _judge_unit(up)
    height = [p[0] * un[0] + p[1] * un[1] + p[2] * un[2] for p in pts]
    low = min(height) if floor is None else floor
    out = []
    for h, n in zip(height, normals):
        down = -(n[0] * un[0] + n[1] * un[1] + n[2] * un[2])
        out.append(1 if down > math.sin(math.radians(limit)) and h > low + 0.6 else 0)
    return out


def judge_draft(normals, pull, draft):
    ''' (the number of surface points that are wall-like -- more than 60 degrees off the pull direction --, and the number of those that lean less than `draft` degrees from the pull direction): the
        angle a face makes with the pull direction is asin |n . pull| '''
    pn = _judge_unit(pull)
    walls = [abs(n[0] * pn[0] + n[1] * pn[1] + n[2] * pn[2]) for n in normals]
    walls = [w for w in walls if w < 0.5]
    bad = sum(1 for w in walls if math.degrees(math.asin(w)) < draft * 0.95)
    return len(walls), bad


def judge_undercut(field, pts, normals, axis, length, steps=40):
    ''' 1 for each surface point from which a line along `axis` meets the material in BOTH directions within `length` (a tool or a mould half coming along the axis cannot reach it), 0 where it does
        not.  The line starts 0.02 mm off the surface, on the air side '''
    an = _judge_unit(axis)
    flat = []
    for p, n in zip(pts, normals):
        q = (p[0] + 0.02 * n[0], p[1] + 0.02 * n[1], p[2] + 0.02 * n[2])
        for sgn in (1.0, -1.0):
            for k in range(1, steps + 1):
                s = length * k / float(steps)
                flat.append((q[0] + sgn * s * an[0], q[1] + sgn * s * an[1], q[2] + sgn * s * an[2]))
    vals = evaluate(field, flat)
    out = []
    for i in range(len(pts)):
        up = any(v <= 0 for v in vals[i * 2 * steps: i * 2 * steps + steps])
        down = any(v <= 0 for v in vals[i * 2 * steps + steps: (i + 1) * 2 * steps])
        out.append(1 if up and down else 0)
    return out


def judge_reach(field, pts, normals, radius):
    ''' How big a ball a cutter can put against each surface point from the air side, in mm, up to `radius`: the largest r of six whose ball (centre p + r n) lies wholly in the air -- the field at the
        centre is at least 0.97 r.  A point in an inside corner sharper than `radius` reaches less; a flat face or an outside corner reaches all of it '''
    flat = []
    for p, n in zip(pts, normals):
        for k in range(1, 7):
            r = radius * k / 6.0
            flat.append((p[0] + r * n[0], p[1] + r * n[1], p[2] + r * n[2]))
    vals = evaluate(field, flat)
    out = []
    for i in range(len(pts)):
        best = 0.0
        for k in range(1, 7):
            r = radius * k / 6.0
            if vals[i * 6 + k - 1] >= 0.97 * r:
                best = r
        out.append(best)
    return out


def judge_share(flags):
    ''' the share of the surface points that are flagged (each point stands for the same area when they were thrown at random) '''
    return sum(flags) / float(max(1, len(flags)))




# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# F. Parts nobody designed for manufacturing, and parts whose fields are not what the rules assume (TEST_DESIGN.md, groups E and F)

def surface_points(field, lines, per_line=3, samples=80):
    ''' Points ON the surface of `field` (where it is 0), found along the lines [(a, b)]: the sign of the field is looked at in `samples` steps along each, and every change of sign is halved down to a
        point (30 times).  At most `per_line` points of each line '''
    out = []
    for a, b in lines:
        pts = [tuple(a[k] + (b[k] - a[k]) * i / float(samples) for k in range(3)) for i in range(samples + 1)]
        vals = evaluate(field, pts)
        found = 0
        for i in range(samples):
            if (vals[i] <= 0) != (vals[i + 1] <= 0):
                p, q, p_in = pts[i], pts[i + 1], vals[i] <= 0
                for _ in range(30):
                    m = tuple((p[k] + q[k]) / 2.0 for k in range(3))
                    if (evaluate(field, [m])[0] <= 0) == p_in:
                        p = m
                    else:
                        q = m
                out.append(tuple((p[k] + q[k]) / 2.0 for k in range(3)))
                found += 1
                if found >= per_line:
                    break
    return out






def scaled_fin_and_slot(t, s):
    ''' The fin of 0.6 mm and the slot of 0.2 mm between two blocks of h4, every length multiplied by `s` and the rules with them: what the tool does must not depend on the size of the part '''
    fin = box((0, 0, 0), (0.6 * s, 12 * s, 10 * s))
    part = union(union(fin, box((6 * s, 0, 0), (12 * s, 12 * s, 10 * s))), box((12.2 * s, 0, 0), (18.2 * s, 12 * s, 10 * s)))
    rule = manufacturing_constraint(min_wall=0.8 * s, min_gap=0.4 * s)
    lo, hi = (-3 * s, -3 * s, -3 * s), (21 * s, 15 * s, 13 * s)
    frame(lo, hi, res=8.0 / s)
    res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
    print(res)
    t.near('the fin is thickened to 0.8 x scale (%.4g mm)' % (0.8 * s), sum(run_length(res, (0.3 * s, 6 * s, 5 * s), (1, 0, 0), 4.0 * s, 6000)), 0.8 * s, 0.06 * s, ' mm')
    t.check('the slot between the blocks is closed (material at its middle)', evaluate(res, [(12.1 * s, 6 * s, 5 * s)])[0] <= 0, '', '')
    a, r, _ = changed_volumes(part, res, lo, hi, n_coarse=200000, n_dense=200000)
    t.near('the material added: fin 0.2 x 12 x 10 and slot 0.2 x 12 x 10 = 48 (times the scale cubed)', a, 48.0 * s ** 3, 0.15 * 48.0 * s ** 3, ' mm3')
    t.check('nothing cut', r < 0.03 * 48.0 * s ** 3, '%.3g mm3' % r, 0)
    cmp_ = compare(part, res, lo, hi, n=200000)
    t.check('no piece of added material floats beside the part', not floating_clusters(cmp_, cell=0.15 * s), '', 0)
    after = manufacturing_analysis(res, constraints=[rule], samples=6000)
    if not after.compliant:
        print(after)
    t.check('the independent check of the result finds no broken rule (%s)' % {k: round(v['share'], 4) for k, v in after.report.items() if v['broken']}, after.compliant, '', 'none')
    return res, aside(part, lo, hi)[0]










# ---------------------------------------------------------------------------------------------------------------------------------------------------------------
# G. THE JOBS (JOB_TESTS.md): a real part made ready for a process, and the judge -- the routines of the kit, with numbers of their own -- says how much of its surface still breaks the
# published rules of that process.  The tool is the library; the judge shares nothing with it that decides what is broken.

_JUDGED = {'wall': 'min_wall', 'gap': 'min_gap', 'overhang': 'overhang', 'draft': 'draft', 'undercut': 'undercut_along', 'radius': 'min_radius'}


def job_shares(field, lo, hi, count, rules, seed=21):
    ''' The share of the surface of `field` that breaks each rule of `rules`, measured by the judge of the kit (random points on the surface, thickness along nine lines, normals of its own
        differences...).  `rules` has the numbers of the PUBLISHED rules (JOB_TESTS.md), e.g. {'wall': 0.8, 'overhang': 45.0, 'up': (0, 0, 1), 'gap': 0.3}: a wall is short under 0.95 of its
        limit, as the sources' own numbers are good to a few per cent.  Returns {rule: share, 'points': how many points were judged} '''
    pts, nrm = judge_surface_points(field, lo, hi, count, seed=seed)
    out = {'points': len(pts)}
    if 'wall' in rules:
        out['wall'] = judge_share([1 if x < 0.95 * rules['wall'] else 0 for x in judge_thickness(field, pts, nrm, rules['wall'])])
    if 'gap' in rules:
        out['gap'] = judge_share([1 if x < 0.95 * rules['gap'] else 0 for x in judge_thickness(field, pts, nrm, rules['gap'], inward=False)])
    if 'overhang' in rules:
        out['overhang'] = judge_share(judge_overhang(pts, nrm, rules.get('up', (0, 0, 1)), rules['overhang'] + 1.0))
    if 'draft' in rules:
        walls, bad = judge_draft(nrm, rules['pull'], rules['draft'])
        out['draft'] = bad / float(max(1, walls))
    if 'undercut' in rules:
        out['undercut'] = judge_share(judge_undercut(field, pts, nrm, rules['undercut'], rules.get('reach', 200.0)))
    if 'radius' in rules:
        out['radius'] = judge_share([1 if x < 0.95 * rules['radius'] else 0 for x in judge_reach(field, pts, nrm, rules['radius'])])
    return out


def job_table(t, label, part, res, lo, hi, rules, count=2500, spare=0.005):
    ''' The table of one job: for each published rule the share of the surface that breaks it by the judge, before and after the tool, and what the tool says of what is left.  Checked: no rule
        is broken over a larger share after than before; and what the judge still finds over `spare` of the surface (a half per cent: the edges where something was added) is LISTED by the tool
        in `fail_states` -- it does not claim a clean result that is not.  Returns (before, after) '''
    before = job_shares(part, lo, hi, count, rules)
    after = job_shares(res, lo, hi, count, rules)
    listed = set(s['rule'] for s in res.fail_states)
    line = '%s: judged on %d / %d points of the surface; the share that breaks each rule, before -> after:' % (label, before['points'], after['points'])
    for k in _JUDGED:
        if k in rules:
            line += '   %s %.1f %% -> %.1f %%' % (k, 100.0 * before[k], 100.0 * after[k])
    print(line)
    print('%s: the tool says: %s added, %s removed (%.1f %% of the part), still broken and listed: %s' % (label, res.added, res.removed, 100.0 * res.share, sorted(listed) or 'nothing'))
    for k in _JUDGED:
        if k not in rules:
            continue
        t.check('%s: %s -- the judge finds it broken on %.1f %% of the surface before and %.1f %% after: not more than before' % (label, k, 100.0 * before[k], 100.0 * after[k]),
                after[k] <= before[k] + 0.004, '%.3f -> %.3f' % (before[k], after[k]), 'after <= before')
        left = after[k] > spare
        t.check('%s: %s -- what is left (%.1f %%) is under %.1f %% of the surface or is listed by the tool (%s)' % (label, k, 100.0 * after[k], 100.0 * spare, 'listed' if _JUDGED[k] in listed else 'not listed'),
                (not left) or (_JUDGED[k] in listed) or res.after is None, '%.3f, %s' % (after[k], 'listed' if _JUDGED[k] in listed else 'not listed'), '< %.3f or listed (or the tool was not asked to re-check)' % spare)
    return before, after


def _bracket_parts():
    ''' The topology-optimized bracket of the examples (an optimizer shaped it: struts of every thickness, leaning every way) as a solid with a true distance, and its faces that carry the
        load -- the plates it is held by and the lugs the load is put on '''
    parts = import_model(_step_file('PivotBearingSupportBracket.STEP'))
    space, (slo, shi) = parts[0]
    plates = union(box((-76, 5, -61), (-45, 36, 29)), box((45, 5, -61), (76, 36, 29)))
    lugs = box((-60, -90, -61), (60, -60, 29))
    holes = union(box((-48, -88, -22), (-26, -44, 22)), box((26, -88, -22), (48, -44, 22)))
    design = exclude(space, holes)
    return design, plates, lugs


def _keep_unchanged(t, label, part, res, boxes, lo, hi):
    ''' The faces that carry the load are as they were: where the part and the result differ, nothing lies inside the boxes that hold them (each grown by 0.5 mm) '''
    cmp_ = compare(part, res, lo, hi, n=150000)
    moved = [p for p, _ in cmp_['added']] + [p for p, _ in cmp_['removed']]
    inside = [p for p in moved if any(all(b[0][i] - 0.5 <= p[i] <= b[1][i] + 0.5 for i in range(3)) for b in boxes)]
    t.check('%s: the plates and the lugs that carry the load are as they were (%d of %d changed points lie in their boxes)' % (label, len(inside), len(moved)), len(inside) == 0, '%d points' % len(inside), 0)






def _frustum(x0, y0, x1, y1, z0, z1, draft):
    ''' a block whose four side faces lean `draft` degrees in toward the top (out toward the top for a negative draft) '''
    k = math.tan(math.radians(draft))
    body = intersection(box((x0 - 2, y0 - 2, z0), (x1 + 2, y1 + 2, z1)), half_space((1, 0, k), (x1, 0, z0)))
    body = intersection(body, half_space((-1, 0, k), (x0, 0, z0)))
    body = intersection(body, half_space((0, 1, k), (0, y1, z0)))
    return intersection(body, half_space((0, -1, k), (0, y0, z0)))


def _control_parts(process, defects):
    ''' A small part built by hand to the published rules of `process` ('fff', 'cnc', 'mould'), and with `defects` the same part with what the rules forbid added -- one defect for each rule '''
    if process == 'fff':
        # an L-bracket of 2 mm walls on the plate and a gusset rib of 2 mm: every wall at least 2, no face looking down, no gap
        part = union(union(box((0, 0, 0), (40, 30, 2)), box((0, 0, 0), (40, 2, 25))), intersection(box((19, 2, 2), (21, 20, 20)), half_space((0, 1, 1), (0, 10, 10))))
        if defects:
            part = union(part, box((8, 10, 2), (8.4, 24, 10)))                                                                  # a rib 0.4 mm thick
            part = union(part, rotate_y(box((30, 6, 2), (32, 16, 10)), math.radians(70.0), center=(31, 11, 2)))                  # a plate whose lower face leans 70 degrees
            part = difference(part, box((25, 5, -1), (25.2, 25, 3)))                                                            # a slot 0.2 mm wide through the base
        return part
    if process == 'cnc':
        # a plate with a window through it, its four corners rounded to 3.2 (the cutter's own radius is 3): a mill reaches every face from above, the walls are 6 mm
        part = difference(box((0, 0, 0), (40, 30, 6)), extrude(rounded_rectangle((6, 6), (34, 24), 3.2), -1, 7))
        if defects:
            part = difference(part, box((8, 8, -1), (11, 14, 7)))                                                                # a slot with sharp corners (radius 0), narrower than the cutter
            part = union(part, box((36, 10, 6), (36.3, 20, 11)))                                                                 # a fin 0.3 mm thick standing on the plate
            part = difference(part, box((-1, 12, 2), (4, 18, 4)))                                                                # a groove in the side: out of reach along z
        # (the corner rule reads the value of the field up to the cutter's radius from the surface: a part made by differences of boxes whose cutters overshoot has only a lower bound of the distance
        # in its window, so it is read through exact_distance, as docs/manufacturing.md says)
        return exact_distance(part)
    # a box open at the top for a mould, walls of 3 mm at the foot with a draft of 2 degrees outside and inside (the inside widening toward the open top), its inside corners rounded to 1 mm
    sharp = difference(_frustum(0, 0, 40, 30, 0, 20, 2.0), _frustum(3, 3, 37, 27, 3, 21, -2.0))
    part = fillet(sharp, 1.0)
    if defects:
        part = union(part, box((38.5, 8, 4), (43.9, 8.4, 14)))                                                                  # a rib 0.4 mm thick standing out of the outside, its root in the wall
        part = difference(part, box((-1, 12, 6), (2, 18, 12)))                                                                    # a groove in the outer face: an undercut along the pull
        part = union(part, box((12, -1.5, 4), (24, 0.3, 12)))                                                                    # a wall that stands upright (no draft) on the outside
    return part


_CONTROL_RULES = {
    'fff': ('FFF', lambda: fff_constraint(nozzle=0.4, overhang=45), {'wall': 0.8, 'overhang': 45.0, 'up': (0, 0, 1), 'gap': 0.3}),
    'cnc': ('CNC', lambda: cnc_constraint(tool_diameter=6), {'wall': 0.8, 'radius': 3.0, 'undercut': (0, 0, 1)}),
    'mould': ('mould', lambda: molding_constraint(min_wall=1.5, draft=1.0), {'wall': 1.1, 'draft': 1.0, 'pull': (0, 0, 1), 'undercut': (0, 0, 1)}),
}


__all__ = [n for n in dir() if not n.startswith('__')]
