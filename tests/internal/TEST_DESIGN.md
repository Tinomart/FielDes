# Manufacturing: the design of the feature tests

Written 2026-10-16 from the code of `python/fieldes/stdlib/manufacturing*.py` (rules, field, check, shell), after the earlier cases turned out to be a pile of the same
thin-wall test in different shapes.  This file says, for every feature: what the algorithm is meant to do, what can go wrong, and the DIFFERENT situations that test it.
A situation that only repeats another one is not in the list.  The results-oriented job tests (a topology-optimized bracket made ready for FFF / CNC / moulding, judged against
published design rules) come AFTER all of this passes.

## How a test passes (three layers; the picture is the proof, the numbers are an aid)

1. **Numbers measured on the result's field, not told by the tool**: a tape measure along a line through the field (`run_length`), material added / cut counted on points
   (`changed_volumes`), the share of the surface that breaks a rule found by the independent check (`manufacturing_analysis`, a second implementation).  The expected value is
   worked out by hand FROM THE RULE'S DEFINITION (what a wall, a gap, a draft IS), never from a constant of the code.
2. **Pictures from the real window**, each from the four standard views (top, front, right, isometric; orthographic for the three axis views) and a zoom on the changed place.
   Nothing is exported: the render is what is judged.  The CLEAN CHECKLIST, for every picture:
   - C1 no stripes, seams, holes or missing faces where something was changed
   - C2 no lip, spike or fragment past the edge of what was changed; the new face stops where the old one does
   - C3 smooth silhouettes: no staircase on a surface that should be a plane or an arc
   - C4 colours right: red only where material was added, blue only where cut, the part's own colour elsewhere (nothing red or blue "by accident")
   - C5 no flicker or doubled surface (two faces on top of each other)
   - C6 no floating speck or crumb beside the part
   - C7 junctions (a wall on a wall, an edge, a rim, a corner) are as tidy as the rest
3. **Invariants** that hold for every case: the result is a field the render draws in seconds; running the tool on its own result changes nothing (second pass); a part that already
   keeps the rule comes out as it was; nothing outside the region / the constraint's reach moves; the tool's own count of added / cut material agrees with the measurement.

Every case below says what it TRIES TO BREAK.  The cases are files of their own (`mfg_*`), each readable top to bottom in the editor: the part built by hand, the
rule asked, the call, the answers by hand with the sum, the checks.

## 0. What the features are, from the code

| feature | what it is meant to do (the algorithm) |
|---|---|
| `manufacturing_constraint` + presets | holds the rules (overhang, min_wall, min_gap, max_bridge, min_radius, draft, undercut_along), directions, region, fix kinds; presets write a process's numbers; several constraints combine by the strictest limit |
| `manufacturing_analysis` (the check) | finds where the surface breaks each rule with its OWN measurements (wall/gap: the shortest of nine lines in a 20-degree cone; overhang/draft: normals; radius: whether a ball reaches in; undercut/bridge: rays), reports share / worst / regions, colours the part red where broken; GRACE = 5 % of the limit is allowed |
| remedy `walls` | where the line along the normal through the material is shorter than the limit, offset the surface outwards by half of what is lacking on each side, over the whole face, up to its edge and not past it |
| remedy `gaps` | fill: each side grows by half the width (+2 %) so the slot is closed flush with the faces; widen: each side goes back by half of what is lacking; read with forward and backward differences so the middle of a slot has no seam |
| remedy `corners` | the library fillet: the part grown by a ball of the radius and taken back, so an inside corner is the arc of a rolling ball; flat faces, bumps and outside corners untouched |
| remedy `undercut` | what lies both under part of the part and over part of it along the direction is filled (the field's smallest value at 17 fixed heights above the point and below it: the edge of the fill is one straight wall under a lip's tip, not a staircase) |
| remedy `draft` | a wall leaning less than the draft from the pull direction is offset outwards in proportion to its depth below the top; floors and tops untouched |
| remedy `overhang` | fill: a gusset under the overhang at the slope asked (discs that fit in the part), up to `max_support_height`; cut: a teardrop of air above a channel; the first layer on the plate is no overhang |
| supports | `support='block'`: a block straight under what still leans out after the remedies, `support_gap` from the part, none in `no_support`, as a separate body |
| build direction free | `build_direction=None`: the way up that leaves the least leaning surface (axes first, then 40 directions), the part is not turned; `oriented()` turns it |
| shell control | `max_change` budget (a remedy that would change more is refused, with why), `keep=` / excluded regions never touched (feathered edge), `region=` of a constraint, `remedies=` selection, `verify` + `fail_states`, notes, second pass |
| result | `added` / `removed` measured, `change_map` (1 added, -1 cut), display: three bodies (grey / red / blue) or ONE body coloured by change for thin layers, `.supports`, `.support_volume`, `oriented()` |
| exports of a print | `export_print` (3MF with part / supports / no-support objects), `export_layers` (DXF), `export_slice` (SVG): the ONE kind of file the tests write (a product feature that writes files) |

## A. The check (`manufacturing_analysis`): does it find what is broken, and only that

The check is the instrument of every later test, so it is tested first and on parts whose answer is known from the definition of the rule.

| id | part | tries to break |
|---|---|---|
| A1 | a staircase wall: one plate whose steps are 0.3, 0.7, 0.74, 0.78, 0.85, 2.0 mm, `min_wall=0.8` | threshold and GRACE of the wall measurement: 0.3, 0.7, 0.74 red; 0.78, 0.85, 2.0 not; the red patches lie ON the right steps |
| A2 | a block with slots 0.2, 0.3, 0.35, 0.39, 0.45, 1.0 mm, `min_gap=0.4` | the same for gaps; which patches are red |
| A2b | a small plate (14 x 8 x 5 mm, 16 cells to the millimetre) with round holes 0.3 / 0.5 / 1.0, `min_gap=0.4` | a round gap against a slot; the plate is small because the check reads the field through differences of a thousandth of the part's size, and a hole of 0.3 in a 50 mm scene is narrower than three of them |
| A3 | a pocket whose four inside corners have radius 0 (sharp), 1, 2.9, 3, 5 and outside corners, `min_radius=3` | the ball test: sharp and 1 red, 2.9 within grace, 3 and 5 not; outside corners never red |
| A4 | walls drafted 0, 0.5, 0.96, 1.2, 3, 10 degrees (leaning in and leaning out), a floor and a top, `draft=1` | draft threshold, the sign of the lean does not matter to the check, floors and tops are not walls |
| A5 | solids with a down-facing side leaning 20, 40, 44, 46, 50, 60, 80 degrees, an up-facing lean, and a flat bottom on the plate, `overhang=45` | overhang threshold, direction (only DOWN-facing), the first layer on the plate is no overhang |
| A6 | six topologies along z: a side groove, a blind pocket from the top, a through-hole along z, a through-hole across z, a closed cavity, a ledge with open air below it; `undercut_along=(0,0,1)` | classification: groove, across-hole, cavity are undercuts; pocket, along-hole, ledge are not |
| A7 | roofs over gaps of 5, 10, 18, 22, 30 mm held at both ends, and a ledge open at one end, `max_bridge=20` | bridge threshold, "held at both ends" (an open ledge is an overhang, not a bridge) |
| A8 | one part with violations of five rules, the constraint scoped to a `region` covering half of it, then two constraints together (fff + cnc) | scoping (nothing red outside the region), strictest-limit merge, the report's `regions` lie where the red is, shares add up |
| A9 | a big plate with ONE thin fin 3 x 3 mm on it | a small violation is not missed by the sampling (`_even_over_area`); the same call twice gives the same report |
| A10 | the composite violation part of A1-A7 turned 37 degrees about z, moved off the grid, mirrored, scaled by 0.5 and 2 (rules and directions follow) | detection does not depend on pose, position or size |

## B. The remedies, one feature at a time

### B1 walls (`min_wall`)
| id | part | tries to break |
|---|---|---|
| W1 | the staircase wall of A1 | thickens only where the wall is thin (0.3, 0.7, and partially 0.74), leaves 0.78 / 0.85 / 2.0 exactly as they were; no step or lip at the borders between steps |
| W2 | a wedge wall 0.2 -> 1.1 mm over 20 mm (a compact scene: 14 cells to the millimetre) | a continuously varying wall: the offset follows it, the thickened profile has no cliff where the wall crosses 0.8 |
| W3 | thin shells that curve: a tube (r 10), a hollow sphere (r 12), a cone, all 0.5 mm | normals that turn: radial tape measures at several places, no stripes (the picture), no flat facets |
| W4 | ends and joins: an open box (rim, vertical edges, floor), a thin wall on a thick one (T), two thin walls at a right angle (L) | the places where a wall ends or meets another: no lip past the edge, junctions as clean as the faces (the places of L1-L3 of the ledger) |
| W5 | features that are not walls: a thin rod (0.5 round), a thin disc, a thin washer ring | a thin thing round in cross-section: thickened on all sides, nothing at its centre or rim |
| W6 | two thin walls 0.3 apart, both `min_wall` and `min_gap` asked | the two rules together: the walls grow into the gap they bound; the result still keeps both or the tool says which it cannot and where |

### B2 gaps (`min_gap`)
| id | part | tries to break |
|---|---|---|
| G1 | two blocks 0.2 apart, fill | the slot is closed FLUSH with the faces, no seam on the middle plane, corners of the mouth square, one piece |
| G2 | the same, `gap_fix='widen'` | the air is as wide as asked (tape), symmetric, the outer shape of the blocks unchanged, no ghost face on the old surfaces |
| G3 | a V-shaped slot, 0 mm at the root to 3 mm at the mouth | a gap whose width varies: filled up to where it is as wide as asked, continuous surface at the end of the fill |
| G4 | round gaps: holes 0.3 / 0.5 mm, and a ring gap 0.2 mm between a pin and a sleeve | gaps that curve; a hole closed whole, a ring closed whole |
| G5 | a slit narrower than the limit at the bottom of a wide slot | "not above the mouth": only the narrow part is filled, the wide part above is left |
| G6 | slots 0.3, 0.38, 0.39, 0.41, 0.5 for 0.4 | thresholds: 0.3 acted on, 0.38-0.41 left alone (grace), nothing at 0.5 |

### B3 corners (`min_radius`)
| id | part | tries to break |
|---|---|---|
| C1 | an L of two walls, radius 6 | the fillet is the arc of a ball of radius 6 (the radius measured at every 10 degrees of the arc), flat faces and the outside corner untouched |
| C2 | corners between two walls at 60, 90 and 120 degrees | the angle of the corner: the arc is tangent to both faces whatever the angle |
| C3 | the vertex where three faces meet in a pocket, and a corner already rounded with a radius ABOVE the one asked and one BELOW | a three-face corner; a rounded corner that is big enough is left alone, one too small is replaced |
| C4 | a pocket narrower than the ball (4 mm wide, radius 3 asked) | a slot the ball cannot enter: filled as a cutter leaves it |
| C5 | a round boss on a plate | an inside corner that is a circle (the ring), not a straight edge |

### B4 draft (`draft`)
| id | part | tries to break |
|---|---|---|
| D1 | an upright 10 x 10 x 30, draft 2 | width at each height = 10 + 2 (top - h) tan 2 degrees (hand), the top as it is |
| D2 | a vertical cylinder | a curved wall becomes a cone: radius at each height |
| D3 | a box, pull direction x | a pull that is not z: the same rule about another axis |
| D4 | walls already drafted 0.5 (asked 1) and 3 (asked 1), a floor | only what is lacking is added; a wall with enough is untouched; the floor does not move |
| D5 | a wall that leans OVER (an undercut) with `draft` asked | what the draft remedy does with a wall leaning the other way (documented behaviour: left to the undercut remedy) |
| D6 | a tall part (200 mm) at 2 degrees with the default budget | the growth at the foot is large; a refusal says why |

### B5 undercut (`undercut_along`)
| id | part | tries to break |
|---|---|---|
| U1 | a groove in the side of a cube | filled level with the faces |
| U2 | a hole drilled across the pull direction | what the rule does with a sideways hole (it is an undercut: filled whole) |
| U3 | a closed cavity inside a block | a cavity no tool can reach: filled |
| U4 | a dovetail slot | an undercut that widens inward |
| U5 | a groove, direction (1,0,1) | a direction that is not an axis |
| U6 | a part with a blind pocket, an along-hole and a ledge | nothing may change where nothing is an undercut |

### B6 overhang (`overhang`, fill and cut) and bridges
| id | part | tries to break |
|---|---|---|
| O1 | a ledge on a wall, 45 degrees | a gusset whose face leans at most 45 (slope measured at several heights), depth within `max_support_height` |
| O2 | the same ledge for 30 and for 60 degrees | the slope follows the angle asked |
| O3 | a horizontal hole in a block, `overhang_fix='cut'` | a teardrop above the hole (apex angle), the rest of the block untouched |
| O4 | an arch / half-pipe tunnel roof and a sphere cavity | an overhang that turns gradually: the surface leans at most the angle everywhere along the curve |
| O5 | a body resting on the plate | the first layer is not an overhang: nothing added under the part |
| O6 | leans of 43 / 44 / 47 / 50 degrees for 45 | the threshold: 43, 44 left, 47, 50 changed |
| O7 | the ledge with the way up x | `build_direction` that is not z |
| O8 | a flat roof across 30 mm, `max_bridge=20` | reported with where, never repaired; the part comes out unchanged |

### B7 supports, build direction
| id | part | tries to break |
|---|---|---|
| S1 | the ledge with `support='block'` and a budget that refuses the gusset | a block of support under the ledge, `support_gap` clearance, none in the `no_support` strip, the part unchanged |
| S2 | an arch with supports | supports under a curved roof: never touch the part (gap everywhere) |
| S3 | support with a `region` | supports only inside the region |
| S4 | the ledge with supports, written as a print | the 3MF has the part, the supports and the no-support volume as three objects of the right volume; the DXF has one layer per height; the SVG slice has the area of the section (the one case that writes files) |
| B1 | a plate on a post, `build_direction=None` | the way up found is the one with the plate at the bottom; `oriented()` turns the part, supports follow |
| B2 | a part whose best way up is oblique (a shelf tilted 30 degrees) | the search over directions, not only the axes |

## C. The shell around the remedies

| id | part | tries to break |
|---|---|---|
| H1 | a part needing two remedies, a budget that fits one | refusal with the reason and the percentage; the one that fits is made; the refused one leaves the part as it was |
| H2 | a part with a `keep` region and an excluded region | the region is identical to the part's field inside (not a micron), and the edge of it feathers: no cliff or ridge round it (pictures) |
| H3 | a constraint with a `region` | changes only inside; a smooth transition at the region's edge |
| H4 | `remedies=['walls']` on a part that also breaks gaps; an unknown remedy name | only the chosen remedy runs; the error names the valid ones |
| H5 | the result of H1 given to the shell again | a second pass changes nothing |
| H6 | a defect the tool cannot fix, `verify` True and False | `fail_states` lists the rule and where; `verify=False` skips the re-check |
| H7 | two constraints together | the strictest of each rule is met |
| H8 | one result of each kind (added only, cut only, both, a thin layer) | the colours: red / blue / grey are where the numbers say; thin layers draw as one body |
| H9 | every remedy's `added` / `removed` against a count on points | the tool's accounting is right |

## D. Presets and validation

| id | part | tries to break |
|---|---|---|
| P1 | `fff_constraint(0.4)`, `fff_constraint(0.6)`, `cnc_constraint(6)`, `molding_constraint(1.5, 2)` | the numbers each writes (wall = 2 nozzles, gap = 1, radius = tool / 2, ...) and that the rules asked are exactly those |
| V1 | every invalid input: overhang 90, draft 0 and 45, negative limits, zero direction, support without overhang, unknown fix names, no constraint | each refusal says WHAT is wrong and WHAT is allowed |

## E. The same part in other poses (metamorphic)

| id | part | tries to break |
|---|---|---|
| M1 | one part with a thin fin, a narrow slot, an inside corner, a ledge: as it is, off the grid, turned 37 and 90 degrees, mirrored, half and twice the size | what is added, brought back to one size, is the same; the same remedies are made; no floating material |
| M2 | the same wall turned and tilted through 12 poses | the wall remedy does not care which way a wall faces |

## F. Parts nobody designed for manufacturing (the application)

| id | part | tries to break |
|---|---|---|
| X1 | the topology-optimized STEP bracket (example 08) under the FFF, the CNC and the moulding preset, one after the other, one cache | organic struts of every thickness and lean: no fragments, no holes, no change in the places that were fine, the picture is clean from every side |
| X2 | a topology-optimized cantilever generated here (box design space, a point load, 30 % material) | a second, different organic part |
| X3 | a lattice of thin struts | a part that is all thin walls and gaps: speed, and a result that is still a lattice |
| X4 | an imported scanned mesh (the example mesh) | a part that is not made of primitives |
| X5 | a surface with texture (noise) | a surface that is never flat: the remedies and the check do not run away |
| X6 | a tiny part (1 mm) and a huge one (1 m), rules scaled | scale: tolerances that were absolute |
| X7 | a hollow part with an inner body, a two-body assembly, an empty shape | nested and multiple bodies; the empty one fails with a clear message |
| X8 | a random part and random rule from a seed, six at a time | parts the code has never seen (`run_sweep`) |

## G. Speed (measured in every window, reported, never hidden)

Each case prints: the seconds of the tool, of the check, and the time the window takes to draw the result.  A simple part: the tool in seconds, the render in seconds.  A heavy
part (X1): minutes at most, said.  A number that grew since the last run is a defect until it is understood.

## H. The one file kind that is written

`export_print` (3MF), `export_layers` (DXF) and `export_slice` (SVG) of a small ledge with supports are written and read back by one case (S4, `mfg_s4_print_files.py`): they are a
product feature that writes files.  No other test exports anything, and no test exports a mesh to look at it.

## I. Which file tests which design id

Decisions made while writing the files: H7 (two constraints together, the strictest limit of each rule) is part of A8, which holds a constraint scoped to a region and then two constraints
on one part; H9 (the tool's own count of added and cut material against a count on points) is made by `generic_checks` in every remedy case, so it has no file of its own; S4 is the file
writer only; the draft/overhang/undercut/gap cases check, besides the number, the independent check of the result.  The first, thinner cases (`w1_fin_old`, `w3_tube_old`, `c3_pocket`) were
replaced by the designed ones below and are gone; `n3_cylinder_old` was a sound control and is now `n3_cylinder`.

| design id | file |
|---|---|
| A1 .. A10 | `mfg_a1_staircase_wall`, `mfg_a2_slots_and_holes`, `mfg_a3_pocket_corners`, `mfg_a4_drafted_walls`, `mfg_a5_leaning_plates`, `mfg_a6_undercut_kinds`, `mfg_a7_bridge_spans`, `mfg_a8_region_and_strictest_limit`, `mfg_a9_small_violation_on_a_big_part`, `mfg_a10_same_part_other_poses` |
| W1 .. W7 | `mfg_w1_staircase_wall`, `mfg_w2_wedge_wall`, `mfg_w3a_tubes` / `mfg_w3b_spheres` / `mfg_w3c_cone_and_ring`, `mfg_w4_open_box` / `mfg_w4_t_junction`, `mfg_w5_disc`, `mfg_w6_walls_and_gap_together`, `mfg_w7_perforated` |
| G1 .. G6 | `mfg_g1_slot_fill`, `mfg_g2_slot_widen`, `mfg_g3_v_slot`, `mfg_g4_holes_and_cavity`, `mfg_g5_slit_under_a_wide_slot`, `mfg_g6_gap_thresholds` |
| C1 .. C5 | `mfg_c1_L`, `mfg_c2_corner_angles`, `mfg_c3_three_faces_and_roundness`, `mfg_c4_slot_narrower_than_the_ball`, `mfg_c5_boss` |
| D1 .. D6 | `mfg_d1_upright`, `mfg_d2_cylinder_cone`, `mfg_d3_pull_along_x`, `mfg_d4_walls_already_drafted`, `mfg_d5_wall_leaning_over`, `mfg_d6_draft_beyond_the_budget` |
| U1 .. U6 | `mfg_u1_groove`, `mfg_u2_tunnel_across`, `mfg_u3_closed_cavity`, `mfg_u4_dovetail`, `mfg_u5_hole_across_the_x_axis`, `mfg_u6_nothing_to_fill` |
| O1 .. O8 | `mfg_o1_ledge`, `mfg_o2_ledge_other_angles`, `mfg_o3_teardrop`, `mfg_o4_arch_and_sphere_cavity`, `mfg_n6_flat_bottom_on_the_bed` (O5), `mfg_o6_overhang_thresholds`, `mfg_o7_build_direction_x`, `mfg_o8_bridge` |
| S1 .. S4, B1, B2 | `mfg_s1_block_of_support`, `mfg_s2_arch_with_supports`, `mfg_s3_supports_in_a_region`, `mfg_s4_print_files`, `mfg_b1_build_direction_free`, `mfg_b2_tilted_shelf` |
| H1 .. H8 | `mfg_h1_budget_refusal`, `mfg_h2_keep_region`, `mfg_h3_region_scope`, `mfg_h4_remedy_selection`, `mfg_h5_second_pass`, `mfg_h6_what_cannot_be_fixed_and_verify`, (H7: in A8), `mfg_h8_colours_of_added_and_cut` |
| N1 .. N8 (controls: smooth or thick parts that keep every rule come out untouched and unreported) | `mfg_n1_thick_wall`, `mfg_n2_sphere`, `mfg_n3_cylinder`, `mfg_n4_torus`, `mfg_n5_rounded_box`, `mfg_n6_flat_bottom_on_the_bed`, `mfg_n7_fields_that_are_not_distances`, `mfg_n8_thick_block_with_hole_and_boss` |
| P1, V1 | `mfg_p1_primitives_match_their_docs` (the primitives the cases are built from, and the presets of `i1`..`i3`), `mfg_v1_every_refusal` |
| E: M1, M2 | `mfg_m1_same_part_other_pose`, `mfg_m2_orientation` |
| F: X1 .. X7 | `mfg_x1_step_bracket`, `mfg_x1_topology_bracket`, `mfg_x3_gyroid_walls`, `mfg_x5_grit_texture`, `mfg_x6a_a_tenth_of_the_size`, `mfg_x6b_ten_times_the_size`, `mfg_x7_hollow_with_inner_body`, `mfg_n7_fields_that_are_not_distances` (a field that is not a distance), the presets on a part with everything: `mfg_i1_fff_sampler`, `mfg_i2_cnc_sampler`, `mfg_i3_molding_sampler` |
| X8 | the seven `mfg_x8_sweep*` files: parts the code has never seen |

X2 (a topology-optimized cantilever made here) and X4 (an imported mesh) are the parts of the job tests (see JOB_TESTS.md), which come after all of the above has passed.
