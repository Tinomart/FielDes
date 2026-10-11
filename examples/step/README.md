# Sample STEP file

The examples in the folder above import this file.

**`PivotBearingSupportBracket.STEP`** (187 kB) is part of FielDes: the bracket that every example that starts from a part uses (01 to 13 and 15),
so they all run as soon as FielDes is installed. It is a sample part downloaded from GrabCAD; it is distributed here at the maintainer's
decision, and its original author's terms are not known. It is not covered by the licences of FielDes (see [`NOTICE.md`](../../NOTICE.md)). If you
are its author or hold its rights and want it taken out, say so in an issue and it goes.

To run an example on a STEP file of your own, put it in this folder and change the path in the script. The scripts place their supports, loads and
regions for the bracket's dimensions (it spans x -75 to 75, y -85 to 35, z -60 to 28 mm), so a different part needs those numbers adjusted; the
importing, the fields and the lattices work with any STEP file.

The first import of a file is cached next to it (`<file>.fieldes-cache.py` and `.fieldes-cache.trees/`); the caches can be deleted at any time.
