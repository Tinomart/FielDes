# Sample STEP files (not included)

The examples in the folder above import these files. They are not part of FielDes: they are sample parts
downloaded from GrabCAD, and their authors' terms are not known to allow redistribution, so they are neither in
this repository nor in the portable package.

To run the examples as they are, put a STEP file of your own in this folder under each name below. The scripts
place their supports, loads and regions for the original parts' dimensions, so a different part needs those
numbers adjusted (the importing, the fields and the lattices work with any STEP file). Or change the path in
the script.

| File name the examples expect | Used by | What it was |
|---|---|---|
| `PivotBearingSupportBracket.STEP` | 01, 05, 08, 10, 15 | a pivot-bearing support bracket |
| `ShaftSupportStand.STEP` | 02, 06, 07 | a shaft support stand |
| `Keukencombinatie.stp` | 03 | a kitchen unit assembly (90 parts, free-form surfaces) |
| `MobileStand.step` | 04, 12 | a phone stand |
| `Bracket.step` | 09, 11 | a small bracket |

The first import of a file is cached next to it (`<file>.fieldes-cache.py` and `.fieldes-cache.trees/`); the
caches can be deleted at any time.
