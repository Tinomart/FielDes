# Manufacturing: the internal cases

**These are the developers' own cases -- about a hundred, each one tries to break one thing on a part made for it.** The showcases to look at are in `tests/verification/manufacturing` (one for each thing the tools do, each made from
one of these cases and run in a real window); the cases here import their helper from the same place (`tests/kit`, one copy). Run one from this folder (File > Open) when a change to the tools needs it.

What this folder is for: to let anyone, the author included, **see** that the manufacturing tools do what they say, on parts of every kind, in the real FielDes window.

## How to use it

1. Open `00_START_HERE.py` in FielDes (File > Open, or drag it onto the window): it lists every case, what each one tries to break, how to open it and how its last run on the final build ended.
2. Open any `mfg_<id>_<name>.py` the same way. The id is the one of `TEST_DESIGN.md` (`a1` the check of a staircase wall, `w2` a wedge wall, `g3` a V-shaped slot, `c4` a slot narrower than the cutter, `o4` an arch, `s2` supports, `h4` the shell's choice of remedy, `x3` a lattice ...).
   The file runs one case, top to bottom: the part built by hand, the rules asked, the tool, the answers worked out by hand, the checks.
3. It draws **the result** (grey where the part is as it was, red where material was added, blue where it was cut) **beside the part as it was**. Turn the model, zoom into the feature
   (View > Top / Front / Right, keys 7 / 1 / 3; the section view for what is inside a part), look at it.
4. The output pane prints one `PASS` or `FAIL` line for each check, with what was found and what was expected.

**The checks are an aid, not the proof.** They were written next to the code they test and can only look for what their author thought of. The proof is what you see:
a fragment beside the part, a lip on a fin, a staircase where a fillet should be, a hole in a wall, a layer that is missing on one face. Judge the picture first.

## What a case holds

* the part, built by hand from boxes, cylinders and spheres, and the constraint it is run for;
* an answer **worked out by hand** where there is one (the sum is in the case), measured on the field with a tape measure (`run_length` in `mfgtest.py`:
  the thickness of a wall, the width of a gap, as the field gives them), and never read from what the tool says of itself;
* the second check of the finished part (`manufacturing_analysis` on the result), which shares no code that decides what is broken with the tool that made it;
* the material added and cut, counted on points of space and compared with the tool's own count; the pieces of added material that touch nothing (a fragment beside the part);
* what the case is trying to break, in its title.

No case exports a mesh. One case, `mfg_s4_print_files.py`, writes the 3MF, DXF and SVG files that a print is made of (they are a feature of the product) into the system's temporary folder
`fieldes_manufacturing_tests`, and reads them back.

## The files

| file | what |
|---|---|
| `TEST_DESIGN.md` | the design of the feature tests: what each feature is meant to do, what can go wrong, the different situations that test it, and which file tests which |
| `JOB_TESTS.md` | the design of the job tests, which come last: a real part (a topology-optimized bracket) made ready for 3D printing, milling and moulding, judged against published design rules |
| `00_START_HERE.py` | the list of the cases, to open first |
| `mfgtest.py` | the mechanics every case shares: reading a field at many points, the tape measure, the comparison of a part with its result, reading a DXF / SVG / 3MF back |
| `mfgkit.py` | the checks and the parts several cases share (`frame`, `generic_checks`, `arc_checks`, the independent judge of the job tests, the generator of parts the code has never seen) |
| `mfg_<id>_<name>.py` | one case each, to open in FielDes |
| `mfg_x8_sweep_<first seed>.py` | seven files, each with six parts generated at random from a grammar (`random_part` in `mfgkit.py`), each with a random constraint, run and checked by criteria fixed beforehand |
