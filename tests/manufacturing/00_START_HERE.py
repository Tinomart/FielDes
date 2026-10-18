# START HERE: the manufacturing tests.
#
# WHAT THIS IS.  FielDes can check a part against what a process asks of it (manufacturing_analysis) and change it until it keeps the rules (manufacturing_optimization):
# thicken a thin wall, close or widen a narrow gap, round an inside corner, put a draft on a wall, fill an undercut, support an overhang, write the files for a printer.
# This folder holds one file for each thing that can happen, so that you can SEE it.
#
# HOW TO USE IT.  This file does nothing but list the others.  Open one of them (File > Open, or drag it onto the window): it builds a part, runs the tool on it, draws the
# result (grey as it was, red material added, blue material cut) beside the part as it was, and prints one PASS or FAIL line per check in the output pane.
# Every file is written to be read from the top: the part, the constraint, the call of the tool, the answers worked out by hand, the checks.  Its title says what it tries to break.
#
# WHAT TO TRUST.  Look at the picture first.  The numbers are an aid: the checks were written beside the code they test.  Turn the part (Shift + left drag), zoom in, use
# View > Top / Front / Right (the keys 7 / 1 / 3) and the section view for what is inside a part.  A fragment beside the part, a lip, a staircase where a fillet should be,
# a hole in a wall: you see them or you do not.
#
# THE DESIGN.  TEST_DESIGN.md says, for every feature, what it is meant to do, what can go wrong and which different situations test it (the ids below are its ids).
# JOB_TESTS.md is the last stage: a real part (a topology-optimized bracket) made ready for 3D printing, milling and moulding, judged against published design rules.
# No test exports a mesh; one file (mfg_s4_print_files.py) writes the print files (3MF, DXF, SVG), because writing them is a feature of the product.

# A.  THE CHECK (manufacturing_analysis): does it find what is broken, and only that?
#   mfg_a10_same_part_other_poses.py                 One part with a plate of 0.3 and one of 0.7 mm, a plate of 2 mm and a slot of 0.2 mm, held to a wall rule of 0.8 and a
#                                                    gap rule of 0.4: as it is, moved off the grid, turned 37 degrees, mirrored, and at half and twice the size with the
#                                                    rules scaled with it: the same four places must be marked and the same amounts short.
#   mfg_a1_staircase_wall.py                         A staircase of six free plates, 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick, for a 0.8 mm wall rule: the check must
#                                                    mark the first three and leave the other three alone.
#   mfg_a2_slots_and_holes.py                        Slots 0.2, 0.3, 0.35, 0.39, 0.45 and 1.0 mm, for a 0.4 mm gap rule: the check must mark the narrow ones and only those
#                                                    (the round holes are in a2b, in a scene small enough to read them).
#   mfg_a2b_round_holes.py                           Round holes 0.3, 0.5 and 1.0 mm across in a small plate (14 x 8 x 5, 16 cells to the millimetre), for a 0.4 mm gap
#                                                    rule: the check must mark the wall of the 0.3 hole -- a round hole is a gap as wide as its diameter, and the commonest
#                                                    narrow gap there is -- and leave the 0.5 and the 1.0 alone; the scene is small because the check reads the surface
#                                                    through differences of 0.001 of the part's size.
#   mfg_a3_pocket_corners.py                         Six square pockets whose inside corners have radius 0, 1, 1.8, 2.7, 3 and 5 mm, for a rule of 3 mm: the check must
#                                                    mark the corners that are well short of the limit (under 2 mm), must not mark those at the radius or above (3 and 5
#                                                    mm), and says what it does in the zone between (2.7 mm); never an outside corner.
#   mfg_a4_drafted_walls.py                          Eight free walls leaning 0, 0.5, -0.5, 0.96, -0.96, 1.2, 3 and 10 degrees off the pull direction, for a draft of 1
#                                                    degree: the check must mark the walls under 0.95 degrees, whichever way they lean, and no top.
#   mfg_a5_leaning_plates.py                         Seven plates leaning 20, 40, 44, 47, 50, 60 and 80 degrees off the vertical, a post with its flat bottom on the plate
#                                                    and a flat roof high up, for an overhang of 45 degrees: the check must mark the faces that look down and lean more
#                                                    than 46 degrees, and the roof, and not the up-facing faces nor the post on the plate.
#   mfg_a6_undercut_kinds.py                         Six blocks of 16 mm with a side groove, a blind pocket, a hole along z, a tunnel across z, a closed cavity and an
#                                                    overhanging ledge, for no undercut along z: the check must mark the groove, the tunnel and the cavity and the others
#                                                    not.
#   mfg_a7_bridge_spans.py                           Roofs held at both ends over gaps of 5, 10, 18, 22 and 30 mm and a ledge held at one end only, for a longest bridge of
#                                                    20 mm: the check must mark the spans over 21 mm and not the open ledge.
#   mfg_a8_region_and_strictest_limit.py             Thin plates on both sides of a plane, a constraint that holds only on one side, and then two constraints on one part:
#                                                    the check marks only what is inside the region, and holds each rule to the strictest of its limits.
#   mfg_a9_small_violation_on_a_big_part.py          A big plate 60 x 60 x 3 mm with ONE thin fin 0.3 x 3 x 3 mm on it, for a wall rule of 0.8 mm: the check must find the
#                                                    one small violation among thousands of samples, and the same call twice must give the same report.
#
# B1.  WALLS (min_wall): a thin wall is thickened to the limit, up to its edges and not past them
#   mfg_w1_staircase_wall.py                         Six free plates 0.3, 0.7, 0.74, 0.78, 0.85 and 2.0 mm thick (3 x 3 mm each) for a 0.8 mm wall rule: the wall fix
#                                                    thickens the thin ones to the rule, leaves the others exactly as they are, and sticks out past no edge.
#   mfg_w2_wedge_wall.py                             A wedge wall 0.2 mm thick at one end and 1.1 mm at the other, over 20 mm (4 mm wide), for a 0.8 mm wall rule: the fix
#                                                    follows the wall, thickens only the part under 0.8 and leaves no cliff where the wall crosses it.
#   mfg_w3a_tubes.py                                 Thin curved walls of 0.5 mm, tubes of radius 3 and 10, a wall of 0.8 mm asked: a wall that curves about one axis,
#                                                    tight and slack.
#   mfg_w3b_spheres.py                               Thin curved walls of 0.5 mm, hollow spheres of radius 6 and 12, a wall of 0.8 mm asked: a wall that curves two ways.
#   mfg_w3c_cone_and_ring.py                         A thin cone and a thin ring (torus) of 0.5 mm, a wall of 0.8 mm asked: a wall that turns along the axis, and a wall
#                                                    round a tube.
#   mfg_w4_open_box.py                               A box open at the top with walls of 0.5 mm, a 0.8 mm wall asked (the four walls and the floor), small enough to be
#                                                    drawn at 14 cells to the millimetre: a wall is 7 cells thick, the layer added to it 2.
#   mfg_w4_t_junction.py                             A T: a wall of 0.5 mm on a wall of 2 mm, small enough for 14 cells to the millimetre; only the thin one may grow.
#   mfg_w5_disc.py                                   A thin disc: a plate of 0.5 mm and radius 4, a 0.8 mm wall asked, drawn at 16 cells to the millimetre (the layer added
#                                                    is 2.4 cells).
#   mfg_w6_walls_and_gap_together.py                 Two walls 0.5 mm thick with a gap of 0.3 mm between them (6 mm long, 4 high, on a base), for a 0.8 mm wall rule and a
#                                                    0.4 mm gap rule together, drawn at 14 cells to the millimetre (the gap is 4 cells): the walls grow into the gap that
#                                                    was narrow already -- the result must keep both rules or the tool must say which it cannot and where.
#   mfg_w7_perforated.py                             A plate with 9 holes and webs of 0.3 mm between them (a small perforated plate), a 0.8 mm wall asked: the webs have to
#                                                    grow and every hole has to stay open; 14 cells to the millimetre.
#
# B2.  GAPS (min_gap): a narrow gap is closed or made as wide as asked
#   mfg_g1_slot_fill.py                              Two blocks 2 mm apart, a 4 mm gap asked: the slot is closed, level with the faces.
#   mfg_g2_slot_widen.py                             Two blocks 0.2 mm apart, a gap of 0.4 mm asked and gap_fix=widen: the slot is made as wide as asked.
#   mfg_g3_v_slot.py                                 A V-shaped slot, 0 mm wide at its root and 3 mm at its mouth, for a 0.4 mm gap rule: the fill closes the slot up to
#                                                    where it is wide enough and leaves the rest of the V as it is, with a continuous surface between.
#   mfg_g4_holes_and_cavity.py                       Closed and narrow spaces: a hollow cube with 0.5 mm walls, and holes of 0.3, 0.5, 0.62 and 1.0 mm in a block for a gap
#                                                    rule of 0.6 mm: the small ones must close, the others stay.
#   mfg_g5_slit_under_a_wide_slot.py                 A narrow slit of 0.2 mm at the bottom of a wide slot of 3 mm, for a 0.4 mm gap rule: only the slit is closed, up to
#                                                    its mouth, and the wide slot above it stays as wide as it was.
#   mfg_g6_gap_thresholds.py                         Slots 0.3, 0.37, 0.39, 0.41 and 0.5 mm wide, for a 0.4 mm gap rule: the 0.3 slot is closed, the 0.39 / 0.41 / 0.5 ones
#                                                    are left alone, and the one inside the ramp of the grace (0.37) must be closed or left, never made NARROWER.
#
# B3.  INSIDE CORNERS (min_radius): the arc of a ball that rolls in the corner
#   mfg_c1_L.py                                      An L of two walls 3 mm thick, an inside corner radius of 6 mm asked: a fillet, the arc of a ball of radius 6.
#   mfg_c2_corner_angles.py                          Three pocket corners of 60, 90 and 120 degrees, for an inside corner radius of 3 mm: the rounding is the arc of a ball
#                                                    of radius 3 touching both faces, whatever the angle, and the faces beyond the arc are untouched.
#   mfg_c3_three_faces_and_roundness.py              A blind pocket with sharp corners (three faces meet at each bottom corner), a through pocket whose corners are rounded
#                                                    to 1.5 mm and one rounded to 5 mm, an inside corner radius of 3 mm asked: the sharp pocket gets rounded vertical
#                                                    edges, floor edges and a sphere at each of its four bottom corners, the 1.5 mm one is made 3 mm, the 5 mm one is not
#                                                    touched.
#   mfg_c4_slot_narrower_than_the_ball.py            A slot 4 mm wide through a block, narrower than the ball of an inside corner radius of 3 mm, and a slot 8 mm wide
#                                                    beside it: the narrow slot is filled and left with the sag of the ball resting on its two edges (0.76 mm deep), the
#                                                    wide one only gets its two floor corners rounded.
#   mfg_c5_boss.py                                   A round boss on a plate, a radius of 2 mm asked where it meets the plate: a ring of fillet.
#
# B4.  DRAFT: a wall that leans too little is given the lean
#   mfg_d1_upright.py                                An upright 10 x 10 x 30, a draft of 2 degrees asked: wider at the foot by 30 x tan 2 degrees, the top as it is.
#   mfg_d2_cylinder_cone.py                          A vertical cylinder of radius 5 and height 20 for a draft of 2 degrees: the wall becomes a cone, the radius at each
#                                                    height is 5 + (20 - z) tan 2 degrees and the top is as it was.
#   mfg_d3_pull_along_x.py                           A block 10 x 10 x 10 for a draft of 2 degrees with the mould opening ALONG X (not z): the faces across y and z lean,
#                                                    their width grows toward the foot (x = 0), the two faces across x stay where they were.
#   mfg_d4_walls_already_drafted.py                  Walls that stand 0, 0.5 and 3 degrees off the pull direction (narrower at the top), for a draft of 1 degree: the first
#                                                    two come out leaning 1 degree, the third is left exactly as it is, and the lean after is measured from the field.
#   mfg_d5_wall_leaning_over.py                      A wall that leans OVER (3 degrees, wider at the top, standing on a base) with a draft of 1 degree and no undercut
#                                                    asked, and then with an undercut along z asked too (a mould): the draft alone leaves it, the undercut rule fills what
#                                                    lies under the lean, and the next pass gives the new vertical face its draft.
#   mfg_d6_draft_beyond_the_budget.py                A block 10 x 10 x 200 for a draft of 2 degrees (the growth at the foot would be 7 mm, 140 % of the part) with the
#                                                    default budget: the tool refuses, says why with the numbers, and the part comes out exactly as it was.
#
# B5.  UNDERCUTS: what no tool or mould half can reach is filled
#   mfg_u1_groove.py                                 A cube with a groove in its side, nothing out of reach along z asked: the groove is filled.
#   mfg_u2_tunnel_across.py                          A tunnel 6 x 6 mm across a block of 20 (along x), an undercut along z asked: a tunnel across the way a mould opens
#                                                    cannot be moulded, it is filled whole.
#   mfg_u3_closed_cavity.py                          A closed cavity of 8 mm in a block of 20, an undercut along z asked: a cavity that no tool can reach is filled, and
#                                                    the block is solid again.
#   mfg_u4_dovetail.py                               A dovetail groove (4 mm at the mouth, 8 mm at the bottom, 6 deep) in the top of a block, an undercut along z asked:
#                                                    what lies under the two lips is filled, and the groove becomes a straight slot as wide as its mouth.
#   mfg_u5_hole_across_the_x_axis.py                 A hole of radius 3 drilled along z through a block of 20, an undercut along x (the mould opens along x) asked: across
#                                                    the way it opens, the hole is an undercut and is filled whole.
#   mfg_u6_nothing_to_fill.py                        Three parts with nothing out of reach along z -- a blind pocket from the top, a hole along z, a ledge with air below
#                                                    -- and an undercut along z asked: nothing may change and the tool reports nothing.
#
# B6.  OVERHANGS AND BRIDGES: a gusset, a teardrop, a bridge that is only reported
#   mfg_o1_ledge.py                                  A ledge sticking out of a wall, an overhang of 45 degrees asked: a gusset under it, every face leaning at most 45
#                                                    degrees.
#   mfg_o2_ledge_other_angles.py                     The ledge of o1 for an overhang of 30 degrees and of 60 degrees: the gusset under it leans at the angle asked (depth
#                                                    under a point d from the tip is d / tan(angle)).
#   mfg_o3_teardrop.py                               A horizontal hole of radius 5 in a block, an overhang of 45 degrees asked with overhang_fix=cut: a teardrop above the
#                                                    hole.
#   mfg_o4_arch_and_sphere_cavity.py                 A tunnel with a round roof (walls 8 mm high, a half-circle of radius 6 on top) and a closed spherical cavity of radius
#                                                    6, an overhang of 45 degrees asked with overhang_fix=cut: the roof of each becomes a teardrop, a pointed arch and a
#                                                    cone, with its apex 6 sqrt 2 = 8.49 mm above the centre and its faces leaning 45 degrees.
#   mfg_o6_overhang_thresholds.py                    Four plates leaning 43, 44, 47 and 50 degrees off the vertical (the lower face looks down), an overhang of 45 degrees
#                                                    asked and the gusset allowed: the two under 46 degrees are left exactly as they are, the two over are given a gusset
#                                                    under their lower face, which then leans no more than 45 degrees.
#   mfg_o7_build_direction_x.py                      The ledge of o1 turned so that the print stands on the x axis (the part with its x and z exchanged, the way up (1, 0,
#                                                    0) given): the same gusset must come out as in o1.
#   mfg_o8_bridge.py                                 A flat roof across a 30 mm gap, a longest bridge of 20 mm asked: it is reported, not repaired.
#
# B7.  SUPPORTS, THE WAY UP, THE FILES OF A PRINT
#   mfg_s1_block_of_support.py                       The ledge with support="block" and a budget of 5 %: the gusset is refused, a block of support is made under the ledge,
#                                                    with a clearance of 0.3 mm to it and none in the strip kept free, and the part itself is unchanged.
#   mfg_s2_arch_with_supports.py                     A tunnel with a round roof in a block, an overhang of 45 degrees asked with support="block" and a budget that refuses
#                                                    the teardrop: a block of support under the part of the roof that leans out, with a clearance of 0.3 mm to the roof and
#                                                    to the walls everywhere, and the part itself unchanged.
#   mfg_s3_supports_in_a_region.py                   Two ledges of the same kind on one wall unit each, a constraint with support="block" that holds only in a region round
#                                                    the first, and a budget that refuses the gusset: supports are made under the first ledge only.
#   mfg_s4_print_files.py                            A small ledge on a wall (10 x 10 x 12) with a block of support under it, written as a print: the 3MF holds the part,
#                                                    the supports and the no-support volume as three objects of the right volumes, the DXF has one layer for each
#                                                    millimetre of height, the SVG slice has the area of the section -- the one case of the suite that writes files,
#                                                    because writing them is a feature of the product.
#   mfg_b1_build_direction_free.py                   A plate on a post (the post under it), the way up of the print not given: the tool finds that the plate has to be at
#                                                    the bottom and turns the part.
#   mfg_b2_tilted_shelf.py                           A shelf tilted 30 degrees (a plate 40 x 30 x 3 with a post standing square on it), an overhang of 45 degrees and the
#                                                    way up left free: of the six axes none holds it without an overhang, and the direction the tool finds, searched over
#                                                    the axes and forty more, leaves less of the surface leaning out than the best axis does.
#
# C.  THE SHELL: budget, kept regions, regions, which remedy runs, what cannot be fixed, the colours
#   mfg_h1_budget_refusal.py                         A fin of 0.6 mm and a budget of 5 % (the thickening would change 33 %): the remedy is refused, the part comes out as
#                                                    it was, and the tool says why.
#   mfg_h2_keep_region.py                            A fin of 0.6 mm with the upper half kept (keep=): only the lower half is thickened.
#   mfg_h3_region_scope.py                           Two fins of 0.6 mm, the constraint holds only in a region round the first: the second stays as it is.
#   mfg_h4_remedy_selection.py                       A fin 0.6 mm thick and a slot of 0.2 mm between two blocks, a wall rule of 0.8 and a gap rule of 0.4 asked, and the
#                                                    tool told which remedy it may make (remedies=): only the one chosen is made; a remedy that no rule asks for makes
#                                                    nothing and the tool says so; a name that is no remedy is refused and the message lists those there are.
#   mfg_h5_second_pass.py                            The ledge made twice: the result of the first pass given to the shell again comes out unchanged.
#   mfg_h6_what_cannot_be_fixed_and_verify.py        A roof over a gap of 30 mm (a bridge: reported, never repaired) and a fin 0.6 mm thick (repaired), a bridge rule of 20
#                                                    and a wall rule of 0.8: with verify=True the tool lists the bridge as still broken, says where, and does not list the
#                                                    fin; with verify=False it does not check what it made: no list and no second check.
#   mfg_h8_colours_of_added_and_cut.py               One part that needs a gusset under a ledge (material added) and a teardrop over a bore (material cut), with a
#                                                    constraint of its own for each: the result is drawn as three bodies -- the part's own colour, red where it was added,
#                                                    blue where it was cut -- and the change map says +1, -1 and 0 at the places the picture colours; a fin thickened by a
#                                                    tenth of a millimetre is drawn as one body.
#
# D.  PRESETS AND REFUSALS: the numbers of a process, the primitives the cases are built from, every refusal
#   mfg_p1_primitives_match_their_docs.py            The primitives the manufacturing tests are built from -- box, sphere, cylinder, cone and torus -- drawn as their
#                                                    documentation says: the volume of each, counted on points, against its formula (found while testing: the cone was not
#                                                    a cone).
#   mfg_i1_fff_sampler.py                            A plate with a thin fin, a narrow slot, an L, a boss and a hole, for a 3D printer with a 0.4 mm nozzle (walls 0.8,
#                                                    gaps 0.4).
#   mfg_i2_cnc_sampler.py                            The same part for a CNC cutter of 6 mm (inside corners of radius 3, gaps of 6, nothing out of reach from above).
#   mfg_i3_molding_sampler.py                        The same part for injection moulding (walls of 1.5 mm, a draft of 2 degrees, nothing out of reach along the pull).
#   mfg_v1_every_refusal.py                          Every refusal of the constraint and of the tool: an overhang of 90 or -5 degrees, a draft of 0 or 45, a wall of -1 mm
#                                                    or of "thin", a direction of (0, 0, 0) or of two numbers, a support without an overhang or of a kind that does not
#                                                    exist, a negative clearance, a fix that does not exist, no constraint, an empty part: each says what is wrong and what
#                                                    is allowed.
#
# E.  THE SAME PART IN OTHER POSES AND SIZES
#   mfg_m1_same_part_other_pose.py                   One part with a thin fin, a narrow slot and an inside corner, made 7 ways: as it is, moved off the grid, turned 37 and
#                                                    90 degrees, mirrored, half the size, twice the size (the rule scaled with it): the answer must be the same.
#   mfg_m2_orientation.py                            The same thin wall (0.6 mm, a 0.8 mm wall asked) turned about the vertical axis and tilted, 12 ways: does the tool
#                                                    care which way a wall faces?.
#
# F.  PARTS NOBODY DESIGNED FOR MANUFACTURING, CONTROLS, FIELDS THAT ARE NOT DISTANCES
#   mfg_x1_step_bracket.py                           The real part: the STEP bracket of the examples, for a printer with a 0.4 mm nozzle that also asks for no overhang
#                                                    over 45 degrees.
#   mfg_x1_topology_bracket.py                       The real part: the topology-optimized bracket (an optimizer shaped it: struts of every thickness, leaning every way),
#                                                    for the same printer.
#   mfg_x3_gyroid_walls.py                           A gyroid sheet lattice with walls of 1.0 mm in a block 20 x 20 x 10, a wall rule of 1.6 mm: a surface that turns every
#                                                    way, with no flat face anywhere: every wall of the lattice, measured along its own normal at eight places on the
#                                                    surface, comes out 1.6 mm thick, and nothing is cut.
#   mfg_x5_grit_texture.py                           A plate 24 x 24 x 4 with a grit texture (bumps and pits 0.3 mm high, 1.5 mm across) on every face, an inside corner
#                                                    radius of 1 mm and a wall of 0.8 asked: a surface that is nowhere flat; the tool must not run away -- nothing added
#                                                    farther than 0.6 mm from the part, the texture still a texture.
#   mfg_x6a_a_tenth_of_the_size.py                   The fin of 0.6 mm and the slot of 0.2 mm of h4, every length times 0.1 (a fin of 0.06 mm, rules of 0.08 and 0.04 mm):
#                                                    the tool does the same thing at a tenth of the size.
#   mfg_x6b_ten_times_the_size.py                    The fin of 0.6 mm and the slot of 0.2 mm of h4, every length times 10 (a fin of 6 mm, rules of 8 and 4 mm): the tool
#                                                    does the same thing at ten times the size.
#   mfg_x7_hollow_with_inner_body.py                 A hollow sphere (outer radius 10, wall 0.5) with a solid ball of radius 4 floating in its cavity, a wall rule of 0.8
#                                                    mm: the shell is thickened on both sides to 0.8, the ball is not touched (look at it in the section view).
#   mfg_n1_thick_wall.py                             A thick wall (5 mm) for a 0.8 mm rule: nothing to do, nothing may change.
#   mfg_n2_sphere.py                                 A sphere of radius 15: a smooth part that keeps every rule, nothing to change, nothing to report.
#   mfg_n3_cylinder.py                               A cylinder of radius 10 and height 30: a smooth part that keeps every rule.
#   mfg_n4_torus.py                                  A torus (ring) of 12 and 4 mm: a smooth part that keeps every rule.
#   mfg_n5_rounded_box.py                            A rounded box 30 x 20 x 15 with 3 mm edges: a smooth part that keeps every rule.
#   mfg_n6_flat_bottom_on_the_bed.py                 A block 10 x 10 x 10 standing on the print bed with an overhang of 45 asked: its flat bottom is the first layer, not
#                                                    an overhang, and nothing may change.
#   mfg_n7_fields_that_are_not_distances.py          A fin of 0.6 mm and a slot of 0.2 mm (a small scene, 12 cells to the millimetre) whose field is not a distance -- the
#                                                    field of the part times 2 (a slope of 2), times 0.5, and times 1 (a true distance) for comparison -- and a wall rule
#                                                    of 0.8 mm and a gap rule of 0.4 mm: a wall is a length and not a number of the field, so each must be measured and
#                                                    made the same.
#   mfg_n8_thick_block_with_hole_and_boss.py         A block 14 x 14 x 8 with a round hole of radius 3 and a round boss of radius 2 that stands 0.5 mm from two of its
#                                                    edges, for a 0.8 mm wall rule: nothing here is thin, so nothing may be added, nothing reported, and the window draws
#                                                    it as fast as the part itself (found by a speed test: a remedy that looks for strips and wedges added specks along the
#                                                    edges of such a part).
#
# G.  THE JUDGE AND THE JOBS: a real part made ready for a process, judged by routines of its own against the published rules (JOB_TESTS.md)
#   mfg_j0_the_judge_on_known_parts.py               The judge of the job tests tried on parts whose answer is known -- a slab 0.6 mm thick, a block, a ledge with a flat
#                                                    underside, a wall that is vertical and a cone that is not, a block with a side groove, an inside corner -- before it
#                                                    is trusted to judge a job.
#   mfg_j1_bracket_three_processes.py                J1, J3, J4: the topology-optimized bracket (the optimizer shaped it for a point load, half the material) made ready
#                                                    for THREE processes -- a 0.4 mm FFF printer, a 3-axis mill with a 6 mm cutter, an injection mould (ABS) -- each judged
#                                                    by the kit's own judge against the published rules of that process: how much of the surface breaks each rule before
#                                                    and after, the faces that carry the load untouched, what the tool says is left.
#   mfg_j2_step_bracket_fff.py                       J2: the STEP bracket of the examples -- a part nobody designed for printing -- made ready for a 0.4 mm FFF printer,
#                                                    judged by the kit's own judge against the published rules (wall 0.8, overhang 45, a gap under 0.3 prints closed): the
#                                                    share of the surface that breaks each rule before and after, and the places that were fine left as they were.
#   mfg_j5_controls_for_each_process.py              J5, J6: controls for each process: a small part built by hand to the published rules (an L-bracket of 2 mm walls for
#                                                    the printer, a plate with a window of 3.2 mm corners for the mill, a drafted open box for the mould) must be found
#                                                    fine by the judge and left alone by the tool; the same part with one defect for each rule (a rib of 0.4 mm, a ledge
#                                                    leaning 70 degrees, a slot of 0.2 mm, a sharp corner, a groove, a wall without draft) must be found broken by the
#                                                    judge, and fixed or listed by the tool.
#   mfg_j7_second_bracket.py                         J7: the same job on a second shape: the bracket the optimizer makes for a different load (the load turned to the
#                                                    side), made ready for the same three processes, judged in the same way: nothing the first bracket showed is a property
#                                                    of that one part.
#
# PARTS THE CODE HAS NEVER SEEN.  Each of these generates six parts at random from the seeds in its name (change a seed in the file and run it: other parts) and gives each a
# random constraint; criteria fixed beforehand are checked on every one (nothing floats beside a part, the independent check of the result, the tool's own counts).
#   mfg_x8_sweep_1000.py
#   mfg_x8_sweep_1006.py
#   mfg_x8_sweep_1012.py
#   mfg_x8_sweep_1018.py
#   mfg_x8_sweep_1024.py
#   mfg_x8_sweep_1030.py
#   mfg_x8_sweep_1036.py

print("Open one of the mfg_<id>_<name>.py files of this folder (File > Open): each one is a test you can read and look at.")
