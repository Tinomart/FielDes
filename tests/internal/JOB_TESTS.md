# Manufacturing: the job tests (a real job, judged from outside)

Written 2026-10-16.  These come AFTER every feature test of `TEST_DESIGN.md` has passed in the real window, with its pictures looked at.  A feature test asks "does the algorithm do what it
is meant to".  A job test asks the question the user asks: **I have this part (a topology-optimized bracket, say) and I want to make it on a 3D printer / a mill / a mould: I write the
constraint, run the program, and is what comes out something that process can really make -- by the published rules of that process and by what parts that are made that way look like?**

## What makes the judge outside

The numbers in the library's presets (`fff_constraint`, `cnc_constraint`, `molding_constraint`) and in the feature cases were typed by the same hand, so a pass there only shows the code agrees
with itself.  Here the judge is:

1. **The published design rules**, each with its source and its number, written in the case file next to the check.  Where sources disagree the file says so and the check is against the
   RANGE, not one number.
2. **Reference parts**: a part built by hand to those rules (the positive control: it must NOT be flagged and must come out unchanged) and a part that breaks them (the negative control: it
   must be flagged, and fixed or reported).  Real examples of what the rules look like in a part, not an abstraction.
3. **A measurement that is not the library's**: the case file measures the output with routines of its own (rays through the field in many directions, normals by its own differences),
   so that what `manufacturing_analysis` and the shell would both be blind to is still seen.  The independent check of the library is run as well, and the two are compared.
4. **The picture**: standard views, zoomed, section views for the inside; the eye test of `TEST_DESIGN.md` (clean checklist C1-C7) applied to the finished job.
5. **The function**: the job is only done if the part still does what it was for -- the load and support faces are untouched (`keep=`), nothing was cut that carried load, the volume
   change is what the remedies explain, and (for the bracket) the load path is still one body.

No mesh is exported by any job test.

## The published rules (verified from the pages on 2026-10-16; re-check before quoting)

### FFF (a fused-filament 3D printer, 0.4 mm nozzle)
| rule | the sources | the range the check uses |
|---|---|---|
| overhang | Hubs, "How to design parts for FDM 3D printing" (hubs.com/knowledge-base/how-design-parts-fdm-3d-printing): up to 45 degrees without support, the layer rests on half the one below; a University of Melbourne lab guide: over 45 needs support; one guide: 45-60 shows droop; manufacturer data (Reiman EON datasheet): 40-55 by filament | 45 degrees from the vertical, tested at 40-55 |
| wall | the 3D Hubs-derived deck (eduhk.hk/3d-arch/talk.pdf): FDM 0.8 mm; PrintPal (printpal.io/docs/3d-printing-design-guide): 0.8 mm (two perimeters) absolute minimum, 1.6 mm (four) recommended for a 0.4 nozzle; GrabCAD tutorial: 1.0 mm; Reiman: 0.5-0.8 by material | at least 0.8 mm must hold; 1.2-1.6 is "good" |
| bridge | the deck: 10-20 mm by material and printer; PrintPal: up to about 30 mm clean | no span over 30 mm without support; 10-20 flagged as "marginal" |
| gap / clearance | GrabCAD: 0.3-0.4 mm for parts that must not fuse; PrintPal: 0.2 mm | a gap under 0.3 mm prints closed |

### CNC milling (a 3-axis mill, cutter along z)
| rule | the sources | the range the check uses |
|---|---|---|
| inside corner radius of a vertical edge | Hubs, "Reducing CNC machining costs: 13 design tips" (hubs.com/knowledge-base/reducing-cnc-machining-costs-13-design-tips) and the Hubs CNC design video: at least one third of the cavity depth (about 1 mm more is better); worked example (Machinemfg): 12 mm deep, 4 mm radius | radius >= depth / 3 |
| cavity depth | Hubs: not more than 4 times the cavity width (the cutter's reach) | depth <= 4 x width |
| wall | Hubs: 0.8 mm metals, 1.5 mm plastics recommended, 0.5 / 1.0 mm possible; Protolabs: features above 0.51 mm | 0.8 mm for a metal part |
| undercuts | Hubs: not made by a 3-axis cutter from one side; T-bone / special tools only | none along the cutter axis, from either side |

### Injection moulding (ABS, a thermoplastic)
| rule | the sources | the range the check uses |
|---|---|---|
| draft | Protolabs, "Draft" (protolabs.com/en-gb/resources/insight/draft): about 1 degree per 25 mm of cavity depth, 0.5 or 0.25 degree better than none, 3 degrees at a shut-off or a light texture, 5 for a heavy one | 1 degree and more, 3 for a textured face |
| wall | Protolabs, "Injection Molding Basics" (protolabs.com/resources/design-tips/injection-molding-basics): ABS 0.045-0.140 in = 1.1-3.6 mm, polycarbonate 0.040-0.150 in, nylon 0.030-0.115 in; uniform walls (sink, warp, voids where thick) | 1.1-3.6 mm for ABS; no wall twice its neighbour |
| ribs | Protolabs basics: a rib 40-60 % of the wall it stands on | rib <= 60 % of the wall |
| inside radius | Fabcon (third party, fabcon.com/articles/?p=309): 0.5-0.75 x the wall; Protolabs: round every sharp corner (flow, ejection) | 0.5 x the wall at least |
| undercut | the mould halves open along one axis: nothing out of reach along it | none along the pull |

## The jobs

Each job is one file (`mfg_job_*.py`), readable from the top like the feature files: the part, the preset as a user calls it, the sources, the judge, the checks, the picture.

| id | job | the part | judged by |
|---|---|---|---|
| J1 | **a topology-optimized bracket ready for FFF** | the bracket the optimizer makes here (box design space, a fixed face, a point load, 30-50 % material, `topology_optimization`), turned to a solid with `exact_distance`; the preset `fff_constraint(nozzle=0.4, overhang=45)` | the FFF table: before, how much of the surface is under 0.8 mm / past 45 degrees / over a 30 mm bridge; after, none (or the tool says where it could not); the picture from every side; function preserved |
| J2 | **the STEP bracket ready for FFF** (the example `PivotBearingSupportBracket.STEP`, a part nobody designed for printing) | the imported part | the same table; the unchanged places (a design that was already fine) are unchanged |
| J3 | **the topology-optimized bracket made for a mill** | the J1 part, `cnc_constraint(tool_diameter=6)` | the CNC table: every vertical inside corner >= depth / 3 (or the 3 mm the tool asks), no undercut along z from either side, walls >= 0.8 mm; the picture; what it cost (the part grows) |
| J4 | **the topology-optimized bracket made for a mould** | the J1 part, `molding_constraint(min_wall=2.0, draft=1.0)` | the moulding table: every wall >= 1.1 mm and no wall more than twice its neighbour where the tool could say, every face off the pull direction by >= 1 degree (or 3 where textured), no undercut along the pull, inside radii >= half the wall |
| J5 | **positive controls**: a bracket built by hand to the rules of each process (an L-bracket with 2 mm walls, a 45 degree gusset, 3 mm radii, 2 degrees of draft, no undercut) | one part per process | it is not flagged by the judge nor by the check, and the tool changes under 1 % of it |
| J6 | **negative controls**: the same bracket with a 0.4 mm rib, a 70 degree ledge, a 0.2 mm slot, a 0.3 degree wall, a sharp corner 20 mm deep | one part per process | every one of those is found by the judge before, and is fixed or reported by the tool after |
| J7 | **the same job, a second shape**: a different load (a torsion), a different optimizer result | a second optimized part | the same tables: the result does not depend on the first part |

A job passes when: (a) every row of its table holds or the tool lists the place it could not fix (and the judge agrees with that list), (b) the positive control is unchanged and the
negative control fixed, (c) the pictures are clean from every side, (d) the load and support faces are exactly as they were, (e) the time of the tool and of the render is written down.

## What a failure means

A job that fails is a defect of the product, never of the test: the test is changed only if the published rule it was written from turns out to be misread.  The fix is made in the
algorithm and the feature cases are run again, then the job.
