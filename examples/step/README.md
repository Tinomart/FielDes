# Sample STEP files

The examples in the folder above import these files.

**One of them is part of FielDes: `PivotBearingSupportBracket.STEP`** (187 kB), the bracket that examples 01, 05, 08, 10
and 15 start from, so those five run as soon as FielDes is installed. It is a sample part downloaded from GrabCAD;
it is distributed here at the maintainer's decision, and its original author's terms are not known. It is not
covered by the licences of FielDes (see [`NOTICE.md`](../../NOTICE.md)). If you are its author or hold its rights and
want it taken out, say so in an issue and it goes.

**The others are not included**: they are GrabCAD downloads too, and nothing has been decided about distributing them.
To run the examples that use them, put a STEP file of your own in this folder under each name below. The scripts
place their supports, loads and regions for the original parts' dimensions, so a different part needs those
numbers adjusted (the importing, the fields and the lattices work with any STEP file). Or change the path in
the script.

| File name the examples expect | Used by | What it was | In FielDes |
|---|---|---|---|
| `PivotBearingSupportBracket.STEP` | 01, 05, 08, 10, 15 | a pivot-bearing support bracket | **yes** |
| `ShaftSupportStand.STEP` | 02, 06, 07 | a shaft support stand | no |
| `Keukencombinatie.stp` | 03, 13 | a kitchen unit assembly (90 parts, free-form surfaces) | no |
| `MobileStand.step` | 04, 12 | a phone stand | no |
| `Bracket.step` | 09, 11 | a small bracket | no |

The first import of a file is cached next to it (`<file>.fieldes-cache.py` and `.fieldes-cache.trees/`); the
caches can be deleted at any time.
