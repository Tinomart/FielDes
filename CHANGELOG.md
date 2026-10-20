# Changelog

## 1.0.0

### The geometry change is one coloured body with a clean edge; no pale red on faces that were not touched
- **The result of a manufacturing shell is ONE body, coloured by the change.** It was three bodies -- the part, the material added in red, the material cut in blue -- and the red body of a fillet or a gusset is a sliver that thins to nothing along its edges, which a mesh cannot hold under one cell (reported on the mill: "clean crisp jagged edges" along both edges of the ring round a boss, on the plate and up the cylinder). A second pass over a result also drew the first pass's material grey (the red body was "the result less its input"). The surface of the result is one smooth surface now and the red/grey boundary is a colour boundary on it.
- **The change colour is a class read from two fields.** It was the part's field at the mesh vertex carried along the part's normal onto the result's surface, in a ramp of 0.15 mm: at the rim of a hole the normal is the top face's, the carried point landed in the hole's air, and an untouched rim read fully red -- the pale red that ran across the flat face from every rim vertex (the star round the hole of the mould, the stripe over a block, the wedges on the top of a boss). It is now the difference of the part's field and the result's at the point, a step over a few hundredths of a millimetre: nothing at any vertex where the result is the part's own, however far off the surface the vertex lies. The viewer cuts the triangles of a result's change colour where it changes, and any triangle longer than eight cells, so that the edge is as fine as the smallest triangles (`cutByColourClass` in `app/src/shape.cpp`; the cutting that was there for the boundary-condition colours did not run for a result, which takes the result branch of the render).
- **Next to a kept region, material that fills is cut at the region's surface and a skin is eased out.** A gusset, the rounding of a corner and an undercut fill are cut at the surface of the region over a tenth of a millimetre (`keep_fill`): they used to be eased over a hundredth of the part, so a gusset under a kept plate lost everything within 2.5 mm of it and the rest floated at the plate's underside (four pieces on the topology bracket's printer result). The draft, a thickened wall and a closed gap are skins grown off a face, and the skin of a kept face reaches out of the region; they keep the wide feather, because cut sharp at the surface of a kept box that is a millimetre wider than its plate the draft's skin stood off the plate as a sheet 0.6 mm thick with air behind it (26 thin walls of 581 points on the mould of the bracket, 4 of 553 now). The hair that the result is grown by (0.0006 mm) is no longer added inside a kept region: there the result is the part's own field, exactly.
- **A third pass over a draft adds nothing**: the draft lattice read the ramp of a result, where the field is not a distance (slope 7), as a wall with no lean and grew a skin on it.
- **A place that is excluded is not judged by `manufacturing_analysis` either**, as the shell leaves it alone: `exclude(part, region)` or `keep=`. A hole of 5 mm that a cutter of 6 mm cannot cut is closed by the rounding of the inside corners, like a gap; excluded, it stays a hole, and the card says what happens to a hole or a slot narrower than the cutter's ball and how to leave one as it is (showcase 03 does).
- **The model tree puts a model under its user only when it stands directly above it.** A model is the child of the first statement that uses it when every row between them is an input of that statement; any other row between them -- another model, a variable, a display line, an import, a loop or a function -- separates them, and the user shows a shadow of the model instead, so that the rows of the tree always follow the order of the script (a topology optimization made at the top, a loop that used its result and the last line of the script that moved it listed the loop above the optimization). A loop, an `if`, a `with` or a `try` is a user as a statement is: the models directly above it that it reads are inside its row. A `def` or a class is not a user (it reads its names when it is called) but separates the rows. The showcases keep `result = res` right under the call that makes it, so the tree reads result over the call over its inputs. (`python/fieldes/outline.py`, `app/src/scenetree.cpp`; the tree that is drawn at once after an edit, before the script has run again -- `predictedScene` -- works out the same rule.)
- **Where the drawing time of a moulded part goes (measured, plain Python and the window; the formulas are exact and keep their cost).** The mould of example 04 draws in 73 s in the window (the mesher alone 27 s at 2 cells to the millimetre). It is the draft: walls 1.2 s, undercut 0.2 s, draft 24.5 s. Of the draft, the closing of the feet of the grown walls (`_close_feet`, the result asked again from a lifted place) is 78 % (12.8 s with it, 2.9 s without): the cells that keep that lookup on their tape (those at a foot, where a cell of half a millimetre is as wide as the wedge it closes) cost seven times a cell without it (2.98 against 0.43 ms), about half a millisecond for each place the part's field is asked in the tape of a cell. The derivative of the exact distance of a box, a square root of a sum of squares, has an interval across the part's own surface that is not bounded, so the gates that read a normal cannot say "no floor here" to the mesher over a flat face. Tried and not kept, each measured: the closing worked out from the lifted normals (the same field, no faster); the closing gated by the scope boxes (the same field; example 04 has none, they would hold more than 40 % of the part); the closing gated by the slope read from values 1 mm above and below the lifted place (the same field at 400,000 points, no faster); a zone of lookups beside the point (not the same field: it steps over a fin 0.5 mm thick); the symbolic derivative of a min or a max written so that the branch not taken is dropped from the tape (12.5 s against 12.8 s); smoothing the depth of the corner ball's fill over the nodes (the teeth of the ring round a boss are the whole fillet's profile wandering 0.04 mm round the circle, not its tail, and the arcs broke: 3.47 mm of air where 3.0 was asked). The kernel lattices draw the same job in 0.1 s (tool 7 s), but the independent check then finds the draft broken on 5.4 % of the surface, the walls on 0.14 % and the undercut on 0.24 %: the formulas stay.

### Your own primitives; Python moves that cannot break the script; path extrude on Ctrl+E; a clean model tree insertion line
- **Primitives menu (menu bar): your own primitives.** *Primitives > Manage primitives...* lists, for the kind of the model that is selected (3D shapes, 2D shapes, lines, points, surfaces, fields), every shape the menus have: a tick makes it a primitive (shown under Primitives), no tick puts it with the Parametric shapes (a built-in primitive can be taken out as well). **Add selection** makes the model that is selected in the model tree -- with every statement of the script that it is made of: the models, the functions and the variables it needs -- a primitive of your own under the name you give it; picking it from any Primitives menu writes it into the script as a model of its own, with names that the script has not got (a `size` that is there already becomes `size_2`: nothing is overwritten). What you chose is kept in ONE file of yours, `primitives.json` in your data folder (`%APPDATA%\FielDes`, `~/.local/share/FielDes`), and is there in every script you open, whether or not the script you made it in still exists. (`python/fieldes/user_primitives.py`, `app/src/primitives_dialog.cpp`.)
- **A move of Python cannot break the script.** Dragging a block of plain Python (a `def`, a loop, a variable) in the model tree used to be allowed above the statement that makes a name it uses -- a function could be dragged above its definition, a call above its `def`. The library now reads the script (`outline.move_check`: what every statement of the top level makes and what it needs, including what the functions it calls need) and a move that would make a statement read a name from another place -- or before it exists -- shows the question "This would break the script": the easy fix moves the statements it goes with along with it, in the order they have, as one undo step; Cancel leaves everything as it is. A drop whose fix would only put everything back where it was (a `def` dropped below the statement that calls it) is refused with a note that says which statements it has to stay in the order of.
- **Ctrl+E on a 2D shape and a path** (a curve) selected together writes `path_extrude(shape, path)`, the shape swept along the path; a 2D shape alone is still `extrude`.
- **The insertion line of the model tree is white** and is drawn under the + that starts a section, not over it, so the place where a click puts the cursor is seen at once.
- **Extrusions can be pulled by their end faces, whatever 2D shape they are made of.** Pressing the face of an extrusion that is parallel to its 2D shape (the top, `b`; the face on the shape's side, `a`) and dragging changes how long the extrusion is, for a circle, a rectangle, a rounded rectangle, a triangle, a polygon, a ring, a profile of points and a union or offset of 2D shapes alike; the walls -- the sides of the 2D shape -- are pulled as the shape's own sides are (a radius, a side of a rectangle, a side of a polygon, and for a profile of points the two points of the outline the wall is between, together, in the plane of the profile). A 2D shape made of numbers of its own now hands them on to its extrusion (`handles(extrude_1, [16, -6, 30, 6, 0, 12])`: the four sides of the rectangle, then `a` and `b`; the rectangle's extrusion used to offer `a` and `b` only, so its walls could not be pulled), and an extrusion of a profile of points is made again of the profile, so it stays a prism that follows the points. A press on the 2D shape itself, drawn over its extrusion, always pulls the 2D shape. (`fieldes/stdlib/profile.py` `extrude`, `Shape::prismWallAt` and the prism wall press in `view.cpp`, the points' own numbers in `interpreter.cpp`.)
- **Profile is a Parametric shape**, not a Primitive: a profile is made of points and its numbers are theirs. The lattices *voronoi_lattice* and *delaunay_lattice* are offered for a body as well as for points (with a body they make the points too), so no entry of the Lattice menu is greyed out for a body.

### Linux packages that open on Ubuntu 22.04 and Debian 12; the viewport backdrop under WSLg; clean walls on lattices
- **The Linux packages are built on Ubuntu 22.04 (glibc 2.35)**, as the release workflow does, so they open on Ubuntu 22.04, Debian 12 and everything newer (the ones made on Fedora 44 needed glibc 2.43). Checked in WSL: the program asks for `GLIBC_2.35` at most and `ldd` finds every library; the `.deb` installs with `apt` and removes again; `scripts/build-linux.sh` now keeps going after a compile error (`-k 0`) so one run shows every file that does not compile, and a GCC-only error in `kernel/src/fea/tetflow.cpp` (`Ar(long(N), long(N), ...)` read as a function declaration) is fixed.
- **The viewport backdrop is the same teal gradient on every platform.** Under WSLg (Mesa's software renderer) it came out black where Windows showed the gradient. Five ways of putting it on the screen were measured in one real window on each platform (the shapes' shader as it was, a gradient painted by the painter before or behind the 3D drawing, a plain clear, and a quad with a shader of its own): the shapes' shader and both painter ways drew black under WSLg (the painter ones on Windows too), the quad with its own shader -- no uniforms, the GL state it needs set where it is drawn (`app/src/background.cpp`) -- drew the identical gradient on both. The depth buffer is cleared after it, so nothing of an earlier frame is in the next.
- **The wall remedy reads the field's own slope, and is a kernel lattice for every part whose field is not exact.** A thin wall was grown only where the slope of the part's field was at least 0.9 and its foot was found as if the field were a distance: a lattice's field (a gyroid's) is a distance to a few tenths only, and over large patches of the surface nothing grew -- the white streaks over a thickened gyroid. The foot is now found with the slope (a Newton step), the surface moves by the value less the slope times the growth (midway between the point and its foot, in the formula), and nothing is gated on a slope above 0.3 (a plateau has no normal). A gyroid of 1.0 mm walls asked for 1.6 now has walls of 1.59 to 1.66 mm along their normals everywhere that was measured (it came out 1.50 to 1.56 before), the tool takes 3 s and the result draws in 3 s (the formula took 4 s and 46 s). The walls lattice of the kernel (`mode 3`) is used for every part that is expensive to read or whose field is not a distance (the slope of the field wanders by more than 0.02 over its surface: a lattice's, a bracket's) and whose limit it can hold (3.4 nodes or more); a part of exact distance fields (boxes, cylinders, planes and arcs) and a limit that is smaller keep the formula, which is exact at the ends of a small plate.
- **The edges of the grown wall are clean (the walls lattice).** Three causes of a ragged rim where a thickened sheet meets the flat face it was cut from were found in the pictures (the same at 5 and at 10 cells to the millimetre, so it was the field and not the picture's grid): (1) a flat face that a sheet meets at a glancing angle had the wedge behind it taken for a thin wall and was pushed out past the block it was cut from -- a wall is two faces **facing each other**, so the face a line ends on must face back at where it began (its normal within about 37 degrees of the way the line goes on, as the check of the result reads a wall; the formula has the same test); (2) the growth switched on and off within a node where its gates meet a crease, and the offset surface carried that as spikes and shards -- the growth is **smoothed** (a box filter of a fifth of the wall limit each way, twice: 0.3 mm for a wall of 1.6, over the nodes that have a growth; the facing is spread by the same reach first, so that the thin place beside its edge is not left short of the limit), and the surface is the part's own moved by a smooth amount. It runs across the crease where a cut face meets a sheet, which is where the rim was ragged, and so it also rounds off the end of a small flat plate by the same reach -- that is why a part with exact distance fields keeps the exact formula. Tried and left: spreading the growth itself (a lip of half the growth past every convex edge), smoothing only between matching normals and leaving out the nodes that have no wall (the rim stays ragged), and reading the facing a little beyond the end of the line (no change) before it is taken off the part's value, so the surface is the part's own moved by a smooth amount; the margin of 4 % that the target thickness carries (so that the check passes what the tool made) moves the target only, not the line below which a wall counts as thin: a wall the rule tolerates (within its 5 % grace) is left as it is; (3) the colour of a change was three hard classes at the mesh's vertices (see the next item). The growth is at most half the wall limit anywhere, and the part's own surface is never touched elsewhere.
- **The colours of a geometry change have soft edges and a wider ramp.** The change was read as a step (0 on the old surface, 1 where material was added) at the vertices of the mesh and drawn in three hard classes, so where two colours met -- on the flat face a lattice was cut from, between the old cross-section of a wall and the layer added to it -- the edge ran along triangles as a zigzag. The colour map blends between the classes with a smooth step and the ramp of the value is 0.15 mm wide at least, so a vertex near the boundary has a value between the classes and the edge is a curve.

### The model tree shows the Python of the script; the insertion point; the prism; extrude on Ctrl+E; the part turns on the screen
- **Python in the model tree.** The tree is a picture of the script: an `if`, a loop (`for`, `while`), `with`, `try`, a function (`def`), a class and a `match` at the top level are rows of their own (a violet tile with the kind in two letters), with what is in them under them -- the variables a header reads (hollow dot, dim), a loop's variable, the parameters of a function, the variables a body assigns (full dot), every other statement, and `else` / `elif` / `except` / `finally` / `case` as branches that hold their statements; blocks inside blocks, to eight levels, at most 80 rows a level. Everything under a block is LOCKED: it cannot be moved, dragged to, renamed or edited in the tree (the code is edited in the code editor); a click on a name selects it in the code, ready to be replaced by typing, a click on a statement selects the statement. A block at the top level can be dragged above or below any other top-level row (a model, a section, another block) -- its lines move in the script, with the comment lines right above it and the blank lines under it, as one undoable edit -- and is refused, with the reason in a red note beside the dragged label, over a row inside a model or a block. (outline.py `python`; ScenePanel rows of type `python` and `pychild`.)
- **The Python menu** (next to Primitives, Parametric shapes, Operations and Engineering, on empty space and on a model): the most basic features of the language as blocks of plain code -- variable, if, if / else, for loop (a count), for loop (a list), while loop, function (def), class, list, list from a loop, dictionary, try / except, with (a file), import, print -- written at the insertion point, as they are (no `name =`, no line that shows them: they are no model); the code is edited in the code editor.
- **The insertion point.** What is added from the tree no longer always goes to the end of the script. The line between two top-level rows (where the + that starts a section shows) is clickable: a press on the line itself, a few pixels each side and away from the circle, puts the INSERTION POINT there and the text cursor of the code editor on that line (the circle still starts a section). The point is drawn as a line along the gap with a small arrow (solid when set, faint while it is only the default, the end of the script) and it is linked both ways: the cursor the user moves in the editor moves the point (inside a statement it is after the statement, on the first column of its first line before it, between statements above the next one). A new shape, condition, simulation, Python block or paste goes above the row the point is above, so the next one lands under it; the point is known by the row below it, so it stays there however the script is edited. Operations still go under the model they work on. A point inside a block of code, or a tree that is not of the text in the editor, writes at the end as before.
- **Prism, and handles on every primitive.** `prism(a, b, c, z0, z1)`: a triangular prism, the triangle of the corners a, b, c (x, y) between the heights z0 and z1, in the Primitives menu; five numbers, five faces, each pulled parallel to itself. The pyramid and the cone are made of faces that have a number each (the kernel's folded their slopes into constants that are no one's number, so a press on a face could only move the shape as a whole, and a first version made of the corners and the height had a pulled face lift the base and move the opposite side, because one face depended on several numbers): the pyramid is its base plane and four slanted faces of the slope it was made with, the cone its base plane and its side (a cone of that slope, at an offset), the way the prism's and the triangle's sides are -- a face that is pulled moves along its normal, parallel to itself, and no other number changes. A cone is exact now (its base radius is the radius asked for; the kernel's was `cone_ang_z` of an angle). The rounded box is made the same way, of its six sides and the rounding (the kernel's tied every side to the opposite one and to the radius: pulling the top face also moved the bottom face and made the rounding bigger). The polygon is made the same way, of one side per number (the kernel's had the centre in every side, and pulling one side moved the centre as well: three numbers changed); sides that are pulled give an irregular polygon whose sides stay parallel to those of the regular one. Every primitive's faces are draggable: the box, sphere, cylinder, torus and rounded box through the kernel's own numbers, the faceted ones through theirs.
- **Ctrl+E extrudes** the selected 2D shape (the viewport or the tree focused): `Operation > Extruding > extrude` on the selection, the length a round number of the shape's size; what is not a 2D shape gets a word of why not. **Export mesh moved to F7 and Ctrl+Alt+E** (it had Ctrl+E): decided while the user was away, because Ctrl+E was asked for extruding.
- **The part turns on the screen.** A manufacturing result whose constraint left the way up of the print free (`fff_constraint(...)` with no build direction) is now DRAWN standing that way, z up -- the geometry, the colours of the change, the supports and the boxes turned together -- and the card says "Way up of the print (x, y, z)  part shown turned". `show_turned=False` draws it where it is; `.oriented()` is the part turned, for a print, either way.
- **A press on a solid is found at the solid's depth, also when a 2D shape is on display.** The picker cleared the depth buffer for the 2D shapes' pass and read the depth only after it, so a press on a body drawn together with a flat shape (a circle to extrude, a sketch) read the far plane as the depth, and a surface drag started from the wrong point (pulling the top of an extruded circle changed its radius). The depth of the bodies is now read before the 2D pass, and only the pixels the 2D shapes cover are replaced by theirs, so the shape on top still wins.
- **The editor cursor moves the insertion point only when the user moves it.** Selecting a new row in the tree moves the text cursor to its line; that move is made by the program and no longer re-aims the insertion point, so two shapes added one after the other stay in the order they were added.
- **Version 1.0.0 is no longer called a beta.** Help > About read "Version 1.0.0 (beta)" (the word was in the dialog's text and in its six translations); it reads "Version 1.0.0" now, and so does CONTRIBUTING.md.
- **The kernel revision in About (and in a bug report) no longer ends in a false "+".** A "+" means changes that are not committed; the build asked `bash -c "git diff ..."`, and on a machine with WSL that is the `bash` of WSL, whose git sees every file of a checkout on the Windows disk as changed: every build said "+". It asks git itself now (`kernel/src/CMakeLists.txt`).

### Manufacturing: one rolling ball for the corners, a gap with no sheet of air over its floor, and a check that finds the ball where it is
- **The inside corners are the kernel's rolling ball for every part.** The corner formula (the library's `fillet`, a ball walked in the part's field by Newton steps along its gradient) was right in a plain corner (an L of radius 6: 152.3 mm^3 against 154.5 by hand) and wrong for a gap narrower than the ball: on the CNC sampler it filled the gap between the fin and the first block to a plateau 0.8 mm ABOVE the mouth (13.8 where the block is 13), with a sharp inside corner where it met the block, and left an enclosed void two millimetres tall in the bottom of a slot 0.3 mm wide; the library's own slot test (a slot 4 mm wide under a ball of 3) failed the same way, the slot filled flush with its mouth and not with the ball's sag. The lattice ball (mode 5), which already did the bracket, filled the gaps right and was wrong in five other ways, each found by measuring and each fixed: (1) it left a crack of air along the middle plane of a gap, where the slopes of the two faces cancel and the node was skipped as having no normal -- a node on a ridge of the field now gets its deficit like any other; (2) the largest value on the ball was read at 96 directions, 20 degrees apart, half a millimetre of a ball of 3 -- the best direction is refined by looking round it, eight ways at a smaller angle each round, to a few hundredths of a millimetre; (3) the part's field is not a distance beyond an edge (a union of boxes has the largest of the distances to the planes), so an L's fillet came out a fifth short -- the kernel makes a TRUE distance on the lattice (`signChangeSources`, `edt3src`, `nearestSourceDistance`: the sources are the points where the field changes sign along an edge of the lattice, which lie on the surface also along the sharp rim of a hole, and a distance transform finds the nearest of them for every node); (4) a tolerance of 3 % of the radius, 0.18 mm for a radius of 6, cut the tails of every fillet short of its tangent points and left the arc a twelfth smaller than the ball -- it is a part of the node now (0.15 of it, 3 % at most); (5) the value of the ball's field away from the surface is the margin of the test, which gains 0.71 for every millimetre along the diagonal of a corner of 90 degrees, so the centre of the rolling ball, 6 mm from both walls and the arc, read 4.24 -- the field of the result is the exact distance to the filled part (a second transform, sources at the sign changes of the ball's own value) and its values near the surface are divided by their gradient; (6) the shell started its result from the part's plain field and united every remedy onto it, so the plain field came back wherever it was smaller than the true distance (2.5 at the axis of a hole in a plate where the rim is 3.0 away) -- a remedy whose field is the exact distance of its own result is the result now, the part's field standing only inside the original part. Tried and not kept: sampling `exact_distance` in the lattice (3 minutes for the mill), and extending the part beyond its box (it extends the block top upward and the ball can no longer rest on a slot's two edges). Measured: the fillet of an L of radius 6 adds 144.9 mm^3 against 154.5 by hand (the last millimetre at each free end is a ball swinging out past the end), its arc within 0.01 mm of the exact one on the diagonal; a slot 4 mm wide under a ball of 3 is solid to the ball's sag at three places across it; a pocket's blind corner adds 159.7 mm^3 (151.9 by hand); the mill job's tool takes 17 s (the formula 4 s, the drawing is a native field and cheaper than the formula's). The formula `fillet` is the fall-back of a small part when no lattice can be made.
- **A gap rule that the corner radius covers is not made twice.** A ball of radius r cannot enter a gap narrower than 2 r (a cutter's radius is half its diameter, its gap rule), so the rounding closes the gap up to the arc the ball rests on; a gap fill made from the part as it was and united with it rose flat to the top of the walls and stood past their ends with new inside corners that nothing rounds. When a constraint asks a radius of at least half the gap (less the grace) and the corners are to be made, the gap step is left out and the card says why.
- **No sheet of air is left over the floor of a slot.** The gap fill grows each side of a gap by half its width, read along the normal of the nearest face -- and a point just above the floor of a slot is nearer the floor than either side, its line along the normal goes up into the open, and it grew nowhere: a slot 0.7 wide under a gap rule of 1.0 was closed with a sheet of air 0.34 mm tall left over its floor, shut in from above (the independent check marked it). A point of the air nearer a face than half the limit is now asked again from the point half the limit above that face along its normal, where a side is the nearest face; where the question there is "a gap" the point is part of the result.
- **The check finds the ball where it is.** The corner rule is read from the centres a ball that touches each sample could have: along the normal, on its sphere, tilted, on cones round it, and now -- for a sample that still reads short -- by looking round the best centre found, eight ways at a smaller angle each round (the centre of a ball that touches a point at a free end of a corner or on the bowl over a hole is 30 to 60 degrees from the normal and has to be located to a few tenths of a millimetre; a corner that is too tight has none at any angle). The clearance is read as the field says it, and the sphere is looked at as a whole, in 120 directions, before the search round the best centres begins. (It was divided by the slope of the field, first at the sample and then at the centre along the normal, where it is 1.1 to 1.3 on an L whose corner is rounded to a radius of 6 and not where the best centre lies; of the 38 samples that were marked there, 33 had a ball that fits, 5.9 to 6.0 mm clear against the 5.7 asked, measured by 1500 rays from the centre to the surface. The L passes the independent check at 0.05 %, where it was marked on 0.58 %.) On the broken test part the check still marks 6 % of the surface for the corner rule (7 % before), on the control nothing.
- **The foot of a grown wall is closed, and the draft and the walls grow in one band that is wide enough.** A wall that is thickened or given a draft grows at the points it is the nearest face of; a point just above the floor it stands on, between the floor and the plane at 45 degrees through the corner, is nearer to the floor, the floor does not grow, and a wedge of air as deep as the growth (0.4 mm at 2 degrees on a wall 12 high, about 1 mm with the walls thickened too) was left under the grown wall: an undercut along the pull at the foot of every wall (18 samples, 0.36 % of the moulding sampler's surface, the one line of 04 that failed). A point that is nearer a floor than a wall, and whose place one growth farther along the pull (away from the floor; the same under a roof) lies in the grown skin of a wall, is part of the result now (`_close_feet`), so the lean of the wall goes on down to the floor. A last stage that filled the result's undercut with the kernel's column fill was tried first and taken out again: the fill takes a column from its first to its last node of the part as soon as any air lies between them, so a sliver 0.3 mm tall turned the whole wall above it into a vertical stripe 0.1 mm proud of the lean (the draft marked on 4.5 % of the surface). Two things the sampler showed on the way: on the middle plane of a slot narrower than the draft makes it, where the slopes of the two faces cancel, a central difference reads no normal and the growth was 0 in a band down the slot (a seam of air 0.15 mm wide) -- there, and only there, the normals are read by forward and backward differences and the larger growth is taken (used everywhere they read a wall of 2 degrees as 1.87); and the growths of the walls and of the draft are added in one offset whose band is 1.2 times their sum and two steps wide (a fin thickened by 0.53 and given 0.42 had a growth of 0.95, more than the ramp of the band of the walls alone, and its face stood plumb: 0.6 degrees where 2 were asked). Measured on the moulding sampler: the independent check marks the draft on 0.02 % and the undercut on 0.02 % of the surface (one sample each of 5000), all eight checks of the file pass, and the tool takes 3 s (11 s before).
- **A second pass of the tool is stable again, and a thickened wall is the limit up to its free edge.** Three things found by measuring what a result handed to the tool again looked like: (1) the band round the surface that a growth is limited to ends in a ramp that rises seven times as fast as a distance, 0.17 mm off the face that grew, and there the field has no slope along the pull at all -- the lean the face was given is gone from it -- so a second pass read the ramp as a wall with no draft and grew a skin there: a sliver 0.1 mm thick standing off the face with 0.05 mm of air between. The draft now grows nowhere that the gradient of the field is more than a fifth over a distance's; (2) the typical slope of a field, which decides whether a part is a distance (the overshoot of the draft and of the corner radius and the choice between the formula and the lattice follow from it), was read at points anywhere within 3 % of the part's size of the surface -- inside those ramps, a spread of 0.93 for a result of the tool -- so a result was always taken for a field that is not a distance and every pass asked 8 % more draft again (a third pass added 130 mm^3 to a part that was drafted); it is read on the surface now, the points walked onto it by Newton steps; (3) the growth of a wall was tapered to nothing over the last 0.17 mm of a face (for a lattice whose slope wanders, to keep the rim clean), which on a part of exact fields left a strip along every free edge where the wall is under the limit: the independent check marked 0.65 % of the surface of a fin 0.6 mm thick and reads 0.02 % without it. An exact field is not tapered; a lattice goes through the kernel, which smooths its rim itself.
- **A result of the walls, the gaps or the draft draws in seconds, not minutes (a small plate of 70 mm: 6 seconds at 2 cells to the millimetre where it was 80).** Found by counting what the mesher does: the field of a remedy was drawn through hundreds of thousands of cells that had nothing in them, as many as the surface has cells times thirty, and they were not skipped because (1) the growth of a remedy carries the kernel's "may be NaN" flag in most cells near a face (the line along the normal is found by a division of something that may be 0 by something that may be 0), and a cell whose bounds carry the flag is ambiguous and keeps its tape unshortened -- nothing below it is pruned, whatever a mask says; and (2) a formula that finds a thin wall by a walk along the normal has bounds that are never 0 near any face, so every cell of the band round the whole surface was worked through to the finest level for a fin and a slot that were a twentieth of it. The growth is now cleared of the flag (`nanfill`: a NaN is no growth) where it joins the part, and the tool puts boxes round the places where a remedy moves the surface at all (found on the points it measures them on, and from the check of the part) and tells the mesher that everywhere else the growth is 0: the result is the same field (compared at 150,000 points near the surface of the printer plate: no point inside in one and outside in the other, the value inside the material within 0.03 mm), and the cells the mesher works through are those round the fin and the slot. Measured on the real formulas, each masked to a cube of 6 mm: the gaps growth drew in 57 seconds and in 1.3 with the flag cleared, the walls growth in 39 and in 1.0.
- **The undercut fill leaves no slit of air along its walls, and the undercut and the corners are one operator on a part that is expensive to read.** (1) The kernel's undercut fill put its wall at the nodes of the columns that hold a cavity, and the edge of a cavity lies anywhere between such a column and the next: the field there is a mix of the fill's value and the far value, and a slit of air up to most of a node wide was left between the floor and the ceiling along every wall of a fill -- the independent check and a line along z both found it (the bracket's mill result: three cavities that were still there with a ceiling 4 to 7 mm over them). The columns round a column with a cavity (the eight next to it) are filled over the span of the cavity as well, so every cell of the lattice that the cavity touches is filled and the wall stands between 0.14 and 1.14 of a node outside the edge, never inside it (+2,500 mm^3 on a fill of 4,400). (2) Done one after the other the undercut fill and the rounding of the corners undid each other: the walls of a fill are sharp inside corners, and the webs that the rounding puts into a crevice cap columns of air the fill had left open. Applied by hand in rounds to the bracket, the share of its surface that broke the undercut rule went 0, 1.5, 1.3, 2.7, 3.0 per cent and the inside radius 14, 6, 10, 5, 9 per cent, and each round of the rounding added another 1,400 to 2,200 mm^3: the loop does not converge. On a part that is expensive to read the two rules, asked together, are ONE operator in the kernel (lattice modes 6 and 7, `_remedy_access`): the air that a ball-end tool of the cutter's radius cannot touch coming along the axis -- from either end -- is filled, where a ball stands when the part is at least its radius away and the tool can go on out from there (the operator can also widen its shaft by the tangent of a draft for every unit it goes out, and a wall with a taper of 2 degrees is checked by hand; the tool does not use it: one degree over a wall of 10 mm is a skin of 0.19 mm, under half a node of the lattice, and the noise of the distances is the lean -- the mould's draft is the draft remedy's, made after the operator, so that the fill's vertical walls get theirs; and on a lattice a wall that has a roof over it and nothing under it leans the other way, wider at the top, because the half of the mould that forms it is the one pulled down -- the usual skin, wider at the foot, shut a pocket of air under the roof: a vertical line a quarter of a millimetre off the wall met the roof above it and the skin below, the undercut broken on 3.5 % of the mould's surface, 1.1 % with the roof-aware skin. The operator and the draft do not settle in rounds -- the operator again samples every surface afresh and a lean of one degree is under the noise of that: the draft broken on 3.7, 48.8, 12.8 % of the walls and the undercut on 3.5, 0, 12.8 %, each round adding 2,000 to 14,000 mm^3; the column fill after the draft shut the pockets but its sharp walls took the inside radius to 16 %; more draft than asked as a margin traded draft for undercut and material: +1 degree took the draft to 0.3 % but added 34,000 mm^3, a quarter of the part). Everything that is left is the union of the balls that fit, so every point of the surface has a ball resting on it and a line along the axis that meets no material, and the result handed to the tool again changes (nearly) nothing; the surface of the fill is the envelope of the balls (the arc of a rolling ball, a leaning wall, a vertical wall at the edge of a cavity nothing reaches). Checked by hand: the fillet of a ball of 3 mm in the corner of an L adds 39.4 mm^3 for 20 mm of height (38.6 by hand), a slot under a plate 2,077 mm^3 (2,080), a wall with a taper of 2 degrees 1,745 mm^3 (1,676). On the topology bracket's mill job the inside radius broken at 9.0 % of the surface before is broken at 0.4 % after, the tool takes 47 s (68 s before), and the independent check, which skips what is kept, marks two places of 0.07 %.

### Manufacturing: the undercut fill, the draft, the walls and the corners are lattices of the kernel, as the overhang is (a mould job on the topology bracket draws in 8 seconds)
- **The same cure for every slow remedy.** The overhang had become a field the kernel works out once on a lattice (`libfive_field_overhang_lattice`, `kernel/src/fields/lattice_field.cpp`), and the printer's job on the bracket took 131 s. The other remedies were still formulas that ask the part's field dozens of times at every point the mesher samples, and a part that costs 26 microseconds a point (the optimized bracket) made the mill's and the mould's drawing take longer than seven minutes -- the render was the slow part. For a part that is expensive to read (`_Context.clamp_calls`) the remedies now come from the same kernel function, in more modes:
  - **undercut** (a negative slope): a lattice laid over the part along the cutter's axis, the part sampled at the nodes with the mesher's pruning, and in every column the air between the first and the last node of the part filled;
  - **draft** (`mode = 2`): the formula of `_remedy_draft` -- a wall leaning less than the angle from the pull direction grows by its height below the top times what its slope lacks -- worked out on the lattice: the part is looked at in a band round its surface as wide as the draft can move it, the normal is the central difference of the values on the lattice, and the field holds the part's value less the growth;
  - **walls** (`mode = 3`): the formula of `_remedy_walls` -- a wall thinner than the limit is offset outwards by half of what it lacks -- on the same lattice, the part looked at inside as well as outside: the thickness is the length of the line along the normal, from the nearest point of the surface through the material, found at 24 places by trilinear reads of the lattice, with the formula's gates; a limit under three and a half nodes of the lattice stays a formula;
  - **corners** (`mode = 5`, any radius, for EVERY part): the rolling ball itself, with no walk -- see the next section.
- **A small part keeps the formulas** for its walls, its draft, its gaps and its undercut (they are exact at any resolution): those lattices are used only where a point of the part costs more than 15 microseconds, or (the walls) where its field is not a distance. `FIELDES_UNDERCUT_LATTICE`, `FIELDES_DRAFT_LATTICE` and `FIELDES_WALLS_LATTICE` set to `1` force the lattice for any part (the proofs), to `0` forbid it. The corners are a lattice for every part.
- **A part held as `exact_distance` is read through its plain field.** Such a part carries the plain field it was made from, which has the same surface, and the walk (about 32 calls of the part at every point) was in every remedy, every rule of the check and every lattice: a bracket held as `exact_distance` gave no verdict in twelve minutes where the same bracket handed to the tool directly takes about one. The tool now reads the plain field everywhere (`_Context.field`) and keeps the walk (`_Context.walk`) for the only two readers that need a distance in mm -- the corner rule of the check, whose ball is a size (and the corner formula `fillet`, which a small part falls back on when no lattice can be made). For every other part the two are the same field and nothing changes.
- **The progress bar** moves one step for each remedy while the lattices are made (that was most of the tool's seconds, with the bar standing still), and the measuring has the rest of it.
- **Measured:** the mould job on the topology bracket (element 5, the optimized part handed to the tool directly, plates and lugs kept, ABS wall 1.5, draft 1 degree; one run in a real window on the final code, 10-20 00:26): the tool 62.2 s and the first complete drawing 8.1 s (coarse level 1.7 s, fine level 6.2 s), 71 s in all (an earlier run of the same code: the tool 90.9 s, the drawing 12.1 s); before the lattices the tool took 55.3 s and the drawing never finished (coarse level 158.8 s, the fine level not done at 420 s). 12,367 mm^3 added (8.6 %): walls 191, corners 214, draft 7,878, undercut 4,369. The mill job on the same part (`cnc_constraint(tool_diameter=6)`): the tool 55.2 s, the drawing 30.4 s, 5,987 mm^3 added (4.2 %: walls 21, corners 1,608, undercut 4,369, no gap to widen). The printer's job (`fff_constraint`, overhang 45): the tool 20.2 s, the drawing 29.3 s, 2,320 mm^3 added (1.6 %: overhang 2,301, walls 19). All three drawings are complete and clean in the pictures of the window (no holes, no shards). The topology optimization's own result draws in about 33 s.
- **The lattices against the formulas, measured:** against the formula the draft adds 3 % less on the bracket (7,878 against 8,090 mm^3) and the walls 25 % more (191 against 153); the tool's seconds are the lattices sampling the part near its surface (62 s on the bracket, one step of the progress bar for each remedy); the corner fill against the exact bisectors of corners of 60, 90 and 120 degrees (the surface 3.000, 1.242 and 0.464 from the apex along the bisector, the exact 3.000, 1.243 and 0.464) and the moulding sampler's hand answers agree; a part held as `exact_distance` is read through its plain field (below).

### Kernel: the derivative of a field, as a field (`Shape.partial`)
- **`s.partial(axis)`** (Python; `libfive_tree_partial`; `libfive/tree/derivative.hpp`) is the partial derivative of `s` with respect to x (0), y (1) or z (2) **as a field of its own**: the symbolic derivative of the expression, by the rules of calculus (sums, products, quotients, powers and roots, the elementary functions; min and max take the derivative of the smaller or larger side, with weights made of `compare`, so where the mesher's interval decides which side it is the derivative is exactly that side's; a call `f(x', y', z')` follows the chain rule with the called field's own partials, each one tape as for any call). A native field (a mesh's distance, noise) has no formula: with `oracle_step` the partial of that node is a central difference over the step, without it `partial` returns `None`. Checked against central differences of the field on 12 fields (a box, a union of boxes, a sphere, calls of calls with a `sin` in the arguments, abs / square / division, min / max mixes, powers and roots, tan / asin / acos / atan, atan2 / exp / log / sin / cos): at most 0.43 % of 3,000 points differ, and those are within a step of a kink, no NaN anywhere. A removable 0 / 0 (the square root of the exact distance to a box is a root of exactly 0 inside it) reads 0.
- **Why: the gradient used to be read by differences of calls, and their bounds are loose.** The mesher decides a cell from the interval of a field over it. The interval of `f(p + h) - f(p - h)` over a cell is as wide as the cell's two calls together, so divided by `2h` it is as many times too wide as the cell is wider than the step: 12 times at 3 cells to the millimetre with a step of a thousandth of the part. Every bound made from a normal was loose, the mesher could not rule out a cell of the band round the surface and refined all of it (an L of 20 mm took 8 GB; the remedies of the manufacturing tool drew about four times slower than they need). The derivative's interval is the derivative's own: one number over a flat face.
- **Used for** the normals of the manufacturing check and remedies (`manufacturing_field._Normals`: each partial is the mean of the derivative asked at the two ends of the segment `p +- h` along its own axis, which is what a central difference is, and is not asked at `p` itself -- exactly on a face the derivative is the mean of the rules on both sides, half a face's, and a face on a round number is hit exactly by the mesher's grid) and for the Newton steps of `exact_distance`, `offset_exact`, `shell_exact`, `round_edges` and `fillet` (below). A field with a native field in it keeps the differences.

### Mesher: a tie of two sides with the same derivative is not ambiguous
- **Where the mesher puts a vertex on an edge it asks whether the point is ambiguous** -- a min or a max whose two sides have exactly the same value, a crease, where the derivative is not one number -- and searches the features there. It asked the VALUES alone, so every tie counted, also `max(0, 0)`: a clamp of a value that is exactly its bound, which is the same derivative on both sides and has nothing to choose. The fields of the manufacturing tool are full of those (every remedy is the part's own field, with a clamp at 0, over most of space). On example 33's part **165,069 of 178,426 intersection points (92.5 %) were ambiguous**, the ambiguity check ran every called field again at every one of them (26.5 thread-seconds), and the search of the features took 65.7. The derivative pass now notes a tie only where the two sides' DERIVATIVES differ (`DerivArrayEvaluator::trackTies`, calls included, in the same pass: no second run of the called fields). (A tape with a native field in it kept the old check at first; it is checked the same way now, the oracle's own ambiguity added: see "The tool measures a remedy only where it can change the part" below.) Measured on the same part and the same machine: ambiguous points 40,098 of 180,954 (22 %), the check 0 s, the features 17.4 s; the work of the leaves 415 to 331 thread-seconds, the draw 65.6 to 54.0 s; the mesh is the same (34,032 collected cells, 3244.9 mm^3 added, as before). `FIELDES_NO_TIE_AWARE=1` returns to the old check, for comparing.
- **Still the cost of a result, and where it is:** per cell, not in the number of cells. With the derivative or with differences for the normals the cells are the same (61,842 in the interval pass, 210,416 leaves, 77,092 of them on the surface); the thread-seconds of the leaves are the derivatives 153 (46 %), the search along the edges 107, the corners 52, the features 17; 32 called fields stand on the average leaf's tape.

### Exact distances, offsets, shells, rounded edges and fillets read the field itself
- **`exact_distance`, `offset_exact`, `shell_exact`, `round_edges` and `fillet` no longer mesh the shape.** They meshed it, took the distance to that mesh (a native field, which every mesh-reading tool then asked at
  every point) and handed that on: a mesh is output, not something a field-driven tool is made from. They now walk from each point along the field's own gradient (`distance_field.py`; the gradient is the
  symbolic derivative above): the distance is the length of a walk of up to three Newton steps, exact for a field that is a distance already -- a plane, a sphere, a cylinder, a union of boxes -- and, at an edge
  or a corner of planes at any angle, the distance to the edge line or the corner point; a step is believed only as far as the face it leads to is really there (walking from the point along the gradient by the
  field's value must end on the surface: above the cut of a block whose cutter overshoots it, the field describes the cutter's walls carried up past the block, which are not the part's). `round_edges` and
  `fillet` roll a ball of the radius in the material and in the air: the centres are where the field is `-radius` or `radius`, the surface of the result is a radius from them, and a walk that ends where the
  field is still short of the radius (two faces facing each other: a slot or a wall narrower than two radii; see "The ball's walk ends at the planes of its walls" below) finds no place for the ball, so the slot is filled and the thin wall removed. Each is made within `reach` of the surface (the shape's
  own field beyond it, so the mesher skips the cells outside the band). The arguments `resolution` and `margin`, which belonged to the mesh, are gone.
- **Measured (real windows, hand answers):** distance to a face / edge / corner / inside of a box 5.0000 / 5.0012 / 7.0720 / -2.0000 (by hand 5, 5, 7.0711, -2); a box 10 offset by 2: 2609.3 mm^3 (2610.5); a
  box rounded by 2: 909.8 (907.7); the fillet of an L, radius 3: 15.51 mm^3 added in 8 mm of the corner (15.45); a slot 2 wide under a ball of radius 2: 285.00 added inside it (285.00); a slot 5 wide, both
  floor corners: 30.95 to 32.85 (32.62); a plate thinner than the ball under `round_edges`: 0.0000 left. About 1.3 µs a point, and the fillet of an L draws in 1.9 s: the version that read the gradient by
  differences did not draw it in 8 minutes and ran out of 8 GB, which is what the derivative above cured.
- **Not as exact as the rolling ball, and said so (KNOWN_ISSUES.md):** where the field is not a distance (a part made with cutters that overshoot it, or of overlapping pieces) a corner is rounded a little short;
  a slot narrower than the ball is filled flush with its mouth instead of with the sag of the ball resting on its two edges; the material of a corner reaches up to 0.4 mm past the end face of the part; and the check marks two
  small regions (0.4 % of the surface) along the rim of a pocket whose corner is exactly at the radius asked (the mesh version marked none).
- **The walk is not drawn: the viewport meshes the field the distance was made from.** The walk has the sign of the field everywhere (its value is the field plus a correction that stays on the field's side
  of zero), so its surface IS the field's, but meshing it costs a dozen calls of the field at every point of the drawing: `exact_distance(block)` of six pockets took the mesher 309 s at its second finest level,
  and the check's display of it 334 s (the mesh version drew the whole case in 110-147 s). A shape made by `exact_distance` now says which field it is drawn from (`_display_field`, carried by `move`, `rotate`,
  `scale`, `reflect` and `colored`; read in `interpreter.cpp recordShape`) and what a tool, a field viewer (`app_support.exact_view_tree`) or an offset reads is still the walk, with its distances. The same
  `exact_distance(block)` draws in 0.4 s at the finest level, the check's display of it in 10.6 s, and the whole of case a3 runs in 145 s with less than 1 GB in the window. A distance that is used for
  something else than showing it (an offset, a shell, a lookup of a tool) is still a walk at every point, about 32 calls of the field: **a part held as `exact_distance` is slow through the manufacturing tool
  and in the drawing of what it makes** (a small printer-rules scene: the plain part 6.7 s in the tool and 8.5 s to draw, held as `exact_distance` 295 s and not drawn in 20 minutes; see KNOWN_ISSUES.md).

### A moulding result is drawn as one coloured body, the card says what each remedy added, and the first hover no longer runs the window out of memory
- **The draft's skin was drawn as spikes** (reported on the moulding sampler: "random spikes on the cylinder, not an actual nice raft"; the thickening of the plate "extends across the edges like playdough, looks really weird and poor"). The field was right: the boss of radius 5 stands out
  of its wall by 0.402 mm at z = 5.5, 0.349 at 7, 0.279 at 9, 0.210 at 11, 0.140 at 13 and 0.087 at 14.5, the same at all 24 angles measured round it ((17 - z) tan 2 degrees). The picture was wrong: a draft is a skin that tapers to nothing at the top of its wall,
  only the walls remedy and the non-additive gaps were marked thin, so the result was drawn as three bodies and the red skin, thinner than a cell of 0.5 mm, came out as spikes. The draft is a thin skin now: the result is one body, coloured by the change at each point of its surface. It costs time: the one body draws the whole surface of the result from the result's formula, where three bodies draw only the bands that changed -- the moulding sampler at 2 cells to the millimetre 72.9 s against 44.2 s (the tool 10 s either way).
- **The card says what the added material is for.** One row for each remedy that was kept ("Wall thickness increased 175 mm3", "Inside corners filleted 62.4", "Draft applied to walls 1035"): it said that 1172 mm3 were added and not why. The draft is measured from the top
  of the whole part -- its parting plane, as in a CAD tool -- so a low wall, the 5 mm plate of the sampler, is thickened by 0.4 to 0.6 mm all the way up, and a wall that reaches the top gets none there.
- **A fixed crash: the window ran to 8 GB when the mouse first rested on the thin red layer of a three-body result.** Found by replaying the sampler's picture sequence in six windows (not the zoom, not the colour probe under the cursor, which reads at once). Every hover asks `nativeDragOk`,
  which listed the variables of the shape under the mouse with the unshared inline `flatten()` -- for the red layer the whole remedy chain, the part called dozens of times -- on the GUI thread (`Shape::buildDeps`). It reads the shared flatten with the calls kept (`walk_deep`) now: the same variables,
  in a time that does not grow with every call.

### The ball's walk ends at the planes of its walls, and the gate is not fooled by two kinks that cancel
- **The CNC sampler's corners looked wrong, and measured, they were:** the part is a plate with a hole, a fin 0.5 mm thick and 12 high, two blocks, an L and a boss, for a cutter of 6 mm (`mfg_i2_cnc_sampler.py`).
  The corners remedy (the library `fillet`, a ball of radius 3 rolled in the air) filled the 4.5 mm gap between the fin and the first block up to z = 14.5 (the block is 13 high: the ball rests on its edge, so the
  gap is solid up to about 13 and the air above it is untouched), left a 1 mm sheet of air next to the fin in the middle of the filled gap, and left a pocket open inside the second gap. Two causes, both in `distance_field.py`:
  - **The test for "no ball fits" was made on three Newton steps that had not arrived.** `closing` and `opening` walked three steps from every point towards the ball's centres and called a point "not reached by
    a ball" when the second step ran against the first (the cosine between them under -0.5). Over a lower block beside a wall the steps bounce between the wall's face and the block's corner at a cosine of
    -0.6 to -0.83, two steps from a centre that is there. The walk (`_ball_walk`) now ends at the point on the planes of the walls it met: each step defines the plane the surface of the centres has where it ended, and
    the end is the point on up to three of them nearest to where the walk began (built plane by plane, so for two flat faces it is the nearest point of the line where the ball's centres meet two walls, and for three the corner
    point; walls less than about 10 degrees apart are followed along the path of the steps). A step to a wall that is not there -- a cutter that overshoots the part carries walls past the part's ends -- leaves its plane out of
    the solve. A ball does not fit where that point is still short of the radius (a ramp from 2 % to 6 % of the radius), lies on a real surface, and the walls face one another (two of the three steps pointing against each other,
    cosine under -0.5). Where the planes' end point is not on the level set but the path of the steps arrived there (to 0.5 % of the radius), the path is the walk (a curve tighter than the ball: the rounded corner of a pocket
    asked to be 3 mm and made 1.5), and the construction is kept only where it verifies itself, otherwise the part's own field stands. Measured on sections whose exact closing was worked out by brute force (a ball of radius 3
    over the centres found on a 0.2 to 0.3 mm grid): in 3D, five sections of 300 points of parts that are unions of boxes, the library walk filled 54 points that the exact closing leaves open and left 39 open that it fills;
    the planes 11 and 0; on parts made with cutters that overshoot (the real case) the final walk is exact over the middle of a rounded pocket corner and at the ends of a slot down to 0.2 mm; in the lab's 2D sections a V-groove of 120 degrees 5.480 against the exact 5.464.
  - **The gate that shuts the construction where the surface is flat was fooled.** `_gate` took the second difference of the field along the three axes at the ball's scale. On the plane x = 6.5 between the fin and
    the block the two kinks cancel exactly (1.5 + 0.5 - 2 x 1.0 = 0): the gate was shut there and the field was the part's own. For `closing` and `opening` the probe is now taken with steps of unequal length along each axis
    (the scale and 0.71 of it), whose second difference no pair of kinks cancels at every distance; `distance()` keeps the symmetric gate (changing it moved the independent check of the distance cases).
  Real windows, the same part and the three sampler files: along the bisector of a pocket corner the fillet's surface is 3.000 mm from the apex at 60 degrees (exact 3.000), 1.242 at 90 degrees (exact 1.243) and 0.464 at 120 degrees (exact 0.464); the fill beside the fin stops at z = 13.5 where it reached 14.5, the sheet and the pocket are gone, the corners remedy adds 1,765 mm^3 and the whole tool 1,828 mm^3 in 12.6 s. The sampler test files have hand answers now (they asked for "any" change, which is why the wrong picture passed): the FFF sampler 8 of 8, the moulding sampler 8 of 8 (with the boss a cone, 0.349 mm out at z = 7 and 0.210 at z = 11 at eight angles), the CNC sampler the gaps solid up to z = 12 and nothing above 14.8, the foot of a wall 0.76 mm high at 1 mm from it, and the 5 mm hole filled in the middle and open at its ends by the 1.34 mm sag of the ball on its rims.
- **Not exact, and said so:** the cap at the mouth of a slot narrower than the ball (the ball rests on the two rims and sags into the slot) is still filled flush with the rim, because it depends on the rims' own distances,
  which the planes of the slot's walls do not carry; a slot narrower than the ball by less than 2 % of its radius counts as fitting.

### The optimization does not re-check its own result unless asked
- **`manufacturing_optimization(..., verify=False)` is the default now.** The shell ran the whole check again on its own result (so that the card could say "places still breaking a rule" and `.fail_states` could list them). The result is a formula
  that asks the part dozens of times at every point (73 evaluations of the part for one of the result on the topology bracket), and the check asks the result about 57 times at each of its samples: **640 of the 800 seconds** of the shell on the
  bracket, 264 of 295 on the small scene. It is the user's `manufacturing_analysis(result)` now; `verify=True` asks the shell for its own (the card then has the row, `.fail_states` and `.fail_totals` are filled, `.after` is the check).
  Without it the shell on the bracket takes 202 s instead of about 800. The cases that test the shell's own list (`h1 h4 h5 h6 d5 d6 n6 o6 o8 s1 s2 s3 u6 w6 i1 i2 i3`, the control and the sweep of the kit) ask for it.

### The tool measures a remedy only where it can change the part
- **The overhang remedy asks the part about a hundred times at every point it is worked out at**, and the tool worked it out at every point it measured it on: 12,000 points over the box, then up to 30,000 more round the places it had changed. On a topology-optimized bracket (a point of the part 30 to 60 microseconds, of the remedy 4 ms) that was 170 of the tool's 200 seconds. The remedy now carries a **guard** (`_guard`: a cheap field -- the height under the print plate, the part's "none above within reach" test -- that proves, where it is at least the part's own value, that the remedy IS the part's value there), and the tool works the remedy out only where it can change which side of the surface a point is on (`_values_of`: a remedy that only adds material leaves a point inside the part where it is, one that only cuts leaves air as air, and where the guard proves it the part's value stands). Exact, not an estimate: the same volumes to the last digit (example 33's part: overhang 3197, walls 40.73, 3238 mm^3 in all; the coarse bracket: 3149.55, 17.67, 3167 mm^3), and the tool on the coarse bracket (optimization element 8) **48.0 s before, 29.0 s now**.
- **The union of the remedies asks the earlier ones only where a later one adds material.** What the later remedy adds that the earlier ones did not was worked out by asking the earlier remedies -- the overhang, 4 ms a point -- at every point of the later one's shell; it is now asked at the points the later remedy adds (a few per cent of the shell). The same count.
- **Mesher: a tape with a native field in it (the optimizer's density) is checked for ambiguity the way every other tape is.** The derivative pass noted a min or max tie with different derivatives only for tapes without a native field; with one, every tie of the values counted, and on the topology bracket **every one of the intersection points (9,302 of 9,302) was ambiguous**: 13 thread-seconds of ambiguity check and search for features in a drawing of 105. The pass now asks the native field where it is ambiguous by its own rules as well (`checkAmbiguous`, after its derivatives) and notes that as a tie: ambiguous points 118 of 9,302, check and search 0.2 s, the same mesh (2,474 cells collected both ways), the drawing 4 to 6 % shorter (`FIELDES_NO_TIE_AWARE=1` returns to the old check).

### The model tree no longer expands what it cannot count
- **A field made of calls that take calls took 8 GB after the script had finished.** After every run the model tree (and the handles) counted the draggable numbers of each row with `Tree::flatten()`, which
  expands a node again every time it is reached; a field built from calls whose arguments are calls (the walk of `exact_distance` kept in a variable) is exponential that way: 8.2 GB within 19 s of the end of the
  script. `Tree::flatten(share, calls, budget)` takes a budget of steps (the model tree gives it two million) and hands back `Tree::invalid()` when it is over, and the row is then a plain row (`expose.cpp`,
  `plainTree`). The same script now stays at 300 MB.

### The step of a gradient is not a round number
- A step of exactly a thousandth of the part's size put the samples of the tape (`p - h`) on the singular plane of every face at a distance `h` from it (the plane where a derivative of the walk is a tie), so
  thin walls and slits grew by one step only (cases w1, w4, h2). The step is multiplied by 1.01731 (`manufacturing_field._Context.step`, `distance_field._scales`).

### Manufacturing: the progress bar follows the tool
- **`manufacturing_optimization` and `manufacturing_analysis` showed an empty bar for the minutes they take** (the bar read "0 %" with the text of the statement). They reported their phases, but the bar of the run takes a report only inside an open operation, and these two opened none, so every report was dropped. Each now runs as an operation of its own, and says what it does: Measuring surface, Evaluating rules (one step for each rule, and for each chunk of samples), Measuring part volume, Measuring effect for each remedy (three steps inside it), Re-checking result, Done. The check that runs inside the optimization reports within its own stretch of the bar (the part's check 5 to 19 %, the re-check 80 to 98 %), so the bar never jumps back. Looked at in a real window while the tool ran: the bar filled to 58 % and said "Measuring effect: Gap closed or widened". The seconds of the tool did not change (17.1 s before, 16.8 and 18.4 s after: the run-to-run difference of this machine).

### Manufacturing: the time of a result is the tool and the drawing, and both were measured
Total time of a result = the seconds the tool takes + the seconds the viewer takes to draw it. Measured on this machine (a laptop processor; run-to-run differences of up to 15 %), at 3 cells to the millimetre:

| | tool + draw before | tool + draw now |
|---|---|---|
| ledge 30 x 40 x 40 mm with its gusset (`mfg_03`, left half) | 5.9 + 72.3 s = 78 s | 1.3 to 1.6 + 36 to 44 s = about 40 s |
| block with a round hole and its teardrop (`mfg_03`, right half) | 16.2 + 49.7 s = 66 s | 1.2 + 29 to 36 s = about 33 s |
| the whole proof `mfg_03` | draw 227 to 280 s | draw 180 s |

- **The check worked many times the points it needed.** To get 2,500 samples on the surface it moved up to 76,000 points onto it (a part that fills its box has half of the box within the band round its surface): 8 of the 12 s that the optimizer took on the block with a hole. A round now works as many points as the samples still wanted need (and draws as many as that takes), so the tool takes 1 to 2 s where it took 6 to 16. The 2,500 samples are a different set of the same kind: the share of the surface that breaks a rule is read to the same few per cent, and a place a single sample marks (example 33's fin foot, 0.04 %) may be marked or not.
- **The overhang remedy gives the mesher a finite range for the height above a point.** Its search for where the line up from a point enters the part counted a step as an entry only if the line was in the air at the start of the step *and* in the part at its end. For a cell that a sample height cuts through, the range of that product is [0, 1], so the range of the entry was the whole reach (25 mm), and so was every disc built on it: half of the cells under the ledge were worked through to the finest level. A step now counts when the line is in the part at its end (the steps after the first entry are longer, so the value is the same at every point), and the range is finite as soon as one step is surely in the part: half of the lookups of the gusset are asked (124 to 65 million), and the overhang remedy of example 33 alone draws in 8 s where it took 24.
- **The supports of a print are a line of the legend and not a card of their own.** A result with `support='block'` showed a second, small card ("Supports", with an amber swatch and a close button) at the bottom of the picture, apart from the card of the geometry change: confusing. The amber blocks are now the fourth entry of the legend of the result ("print supports", under added, unchanged and removed), shown when the scene has them, in the six languages.
- **The card says which way up a free print stands.** With `build_direction=None` the tool searches for the way up that leaves the least of the surface leaning out (the three axes and 40 more directions) and had shown it only in the output pane and in `.build_direction`. The result card now has the row "Way up of the print" with the direction it found (in the six languages), and `result.oriented()` -- the part turned to stand that way -- can be drawn as it is (`mfg_b1_build_direction_free.py` draws it beside the part as it was).
- **The card counts all the places the re-check found.** `fail_states` lists the five biggest places of each rule, and the card counted only those: on the moulding sampler it said 1.9 % of the surface where the re-check had found 6.9 % (12 places of one rule). The card and the output now count all of them (`.fail_totals`: for each rule the places and their share of the surface), and the output says "N more places, x % of the surface, not listed above".
- **The mesher can say where its time goes.** With `FIELDES_TIMING=1` the window's log ends with `[dc]` lines: the cells of the interval pass and the time they took, the leaves placed (those on the surface and those with nothing in them), and how long the corners, the search along the edges and the derivatives took. It found the two causes above.
- **Measured and not kept:** a quick search along the edges for every field that calls others (no change in time on example 33: 62 s with and 61 s without; an earlier try made 23 % more vertices, up to 0.25 mm off the surface); skipping the shortened tape of a short called field, and copying the table of those tapes only when one changes (no change in time); writing the blend of a remedy with the part as `w*remedy + (1-w)*part` (30 % fewer cells, no change in time); 4 depths of discs instead of 6 (a third quicker, but the teardrop comes out 61 mm^3 where 107 is right); fewer steps in the search for the entry (no change in the gusset); **a walk of eight lookups, each at the place the one before it ended, instead of the 48 places for how far up the part starts** (a sphere walked to a surface: 6 times fewer lookups at a point, the same value, and on the topology bracket's cube **a third slower to draw**: 90.1 thread-seconds against 60.6 for the same 16,500 leaves -- a lookup whose place depends on another has a range as wide as the two together, eight in a chain multiply, and the mesher could not rule out the cells under the part; a point is cheaper, the drawing is what costs).
- **Still slow, and why.** The wall remedy of this version draws about three times slower than that of the first release candidate (a fin of 0.6 mm: 16 s where it was 5 s, at 3 cells/mm): it carries the later fixes (the edges of a disc no longer grow a lip, a wall is not read at an edge, the remedy is exact at the edges of the fin) and about 20 lookups of the part for every cell near the fin. Example 33 (a fin, a post and a ledge) draws in about 45 to 55 s after a tool of 9 s. Draw such a part at 2 cells to the millimetre (about 2.3 times quicker) or 1.5 first.

### Kernel: a call -- a field asked at a computed point, without a copy of it
- **`s(x', y', z')`** (Python; `libfive_tree_call`; opcode `CALL`) is the value `s` has at a point worked out from `x, y, z`. It is what `s.remap(x', y', z')` means, evaluated in another way: a remap is a substitution that is **copied** into every place it is used, a call keeps `s` once and every call asks that one `s`. Use a remap where a field is used once (every transform), a call where the same field is asked at many places or is large. Values, derivatives (so dragging a handle), sharp-edge features, the renderer's interval pruning, `var()`s inside the called field, nesting and saving agree with the remap: call and remap differ by at most 2.4e-7 in values and in derivatives, no sample falls outside a call's interval, the meshes have the same volumes, and a handle dragged on example 29 bends all three legs.
- **Copies replaced by calls:** `smooth`, the images of a symmetry, the placement of imported parts (the STEP importer and the tessellated import: a part that stands in many places is one field and a call at each) and the lookups of the manufacturing check. Measured: the bracket's `manufacturing_optimization` takes 29 s and 307 MB (with copies: 97 s and 3.9 GB); `smooth` and the placement agree with the formulas they replace to 4e-6; Keukencombinatie (90 parts) imported fresh matches the old import to 5.5e-4, and its stored trees are 75,741 bytes where they were 324,815.
- **`Tree::optimized()` counts the ways through a call's arguments** when it decides whether a graph is too branching to optimise (it did not, and took 1.9 s per chunk over 2,432 nodes).
- **The wrapper that rebuilt a moved mesh or data field is gone.** Moving or scaling a node whose values are computed by code (an imported mesh, a data field) used to rebuild it inside another node of its own; now the node is called at the moved point (a moved mesh gives the same field and the same mesh). The renderer's cheaper edge search (10 points, for a part that holds such a node) also applies to a part that a call places.
- **Fixed: the mesher could die on eight threads** for an imported mesh: a cell took an edge's intersection from its neighbour without checking that the neighbour had one. 3 of 10 runs of the plain mesh crashed, 8 of 10 when it was placed twice (the old formula crashed as often as a call); 0 in 30 runs now.

### Imported meshes: drawn in seconds, not a minute
- **The bracket of example 12** (an STL of 232,764 triangles, drawn at resolution 2) is meshed in **1.5 to 2.2 s** on eight threads, where it took 48.8 s; in the window it is **drawn in 1.66 s** (the coarse levels after 0.13 and 0.39 s; it was 5.24 s after the first of the fixes below). A part placed twice (a call of a call) takes 0.3 s where it took 30 s.
  The field is the same: its values at 200,000 points, half of them within a few mm of the surface, did not change at all, and an open and a closed sphere agree with a distance worked out by brute force to 1e-6.
- **Why it was slow, measured.** The distance to a mesh asked the winding number of the whole mesh (a sum over a tree of its triangles) for **every point found inside**, millions of them; it is now asked **once for each face** that is the nearest feature of such a point
  (168,000 of the 232,764 faces here, and none disagreed). That was 48.8 s to 4 s. The rest: the tree the queries walk is repacked once it is built, one 64-byte line per node and the corners of a leaf's triangles one after the other (the queries had read them from all over the memory);
  where only a side is asked, the winding number takes a far leaf as one cluster like the larger ones (the number that is the field near a hole keeps its exact leaves); the mesh field remembers its last 256 points with value and gradient (the renderer asked for a corner again for each edge ending in it, and for the value and the gradient at the two points a search ends with:
  8.2 million searches through the tree were counted for the bracket, each of them a walk over some 25 nodes and 7 triangles); and the renderer's search for the crossing of an edge takes 4 points where it halved 10.

### The evaluator: passes of a size the compiler knows, and a check made only where it is read
- **Both interpreters of the evaluator (values and derivatives) run a pass of 8 to 192 points (derivatives: 8 to 64) with block sizes known to the compiler.** Each clause had set up a loop of a size not known until run time, which cost about as much as the clause itself at the 8 to 32 points of most passes.
  A row of the values is 1,088 bytes (17 cache lines) where it was 1,024, so that the rows of a tape no longer fall in the same few sets of the cache. The numbers are the same operations in the same order: the mesh of the manufacturing result below has the same 29,571 vertices as before, each at a distance of 0.000000 from one of the old ones.
- **The renderer asks whether a corner of a cell is ambiguous** (a min and a max with equal sides; every field that is called is run again to find out) **only for a corner whose value is exactly 0**, the only corner for which the answer was ever read.
- **Measured** on the manufacturing result of the gusset example (`mfg_03`, 3 cells/mm, 90 x 40 x 40 mm), in the window: **all three bodies drawn in 226.7 s where it took 280.6 s**, the finest level of the heaviest body 134.5 s where it was 175.3 s. What is left is the work itself: a point of that field is about 118 lookups of the part (1.45 million cells, 4.8 million corners, 0.47 million edges searched at that resolution, on a 1.3 GHz laptop processor).
  A resolution of 2 cells/mm draws the same part about four times faster (the first coarse picture is there after some 6 to 18 s at either).
- **Tried and not kept:** a search of 4 points per edge for every field that calls others (it was 20 % faster in the edge search): at the bracket of 1/1024 of a cell it left 23 % more vertices (cells no longer merge at the quality asked) and vertices up to 0.25 mm off the surface where the 64-point search leaves 0.06 mm. A field with a mesh in it keeps its 4-point search.

### Examples that opened wrongly
- **Example 27 (copies and linked copies) drew one big block.** Its `handles(...)` lists were in the order boxes and cylinders had before handles were redesigned (face positions; radius first), so the second lug was a distorted box and the bolts were enormous, and the render region grew to hold them
  (the region line of the script was rewritten when the file ran). The numbers are in the order `handles()` uses now (box: centre and half-size on each axis; cylinder: x, y, radius, z from, z to): the plate, the thicker lug and the row of bolts are drawn.
- **Examples 03, 14 and 20 rewrote their own `view.set_bounds` line** the first time they ran, because their models reached outside the region they asked for (by 20 mm, 7 mm and 8 mm). The regions hold the models now, so an example is not changed by being opened.
  All 33 examples were run again in real windows on the final build, from empty caches: every one finishes with its picture and no error line.

### Found and fixed by the manufacturing test round (2026-10-16 and 17)
- **`cone(radius, height, base)` drew the wrong cone.** Its field was `max(-z, cos(a) r + sin(a) z - height)` with `a = atan2(radius, height)`: the base radius came out sqrt(R^2 + H^2) (27.7 mm for a cone of radius 12 and height 25) and the tip at H / sin(a). The plane through the tip is
  `cos(a) r + sin(a) (z - height)`: base radius R at z = 0, tip at z = H. Found by `tests/manufacturing/mfg_p1_primitives_match_their_docs.py`, which draws each primitive the tests are built from and counts its volume on points against its formula.
- **A smooth part that keeps the corner rule came out with specks on it.** The corner remedy is the library's `fillet`, the part grown by a ball and taken back, each time by its distance to a mesh -- good to about a hundredth of a millimetre. Where the part has no inside
  corner that is the part's own field to that much and the remedy added a layer a hundredth of a millimetre thick wherever the mesh fell short: a ring (torus) of 12 and 4 mm came out with *3.9 mm^3 added* on the card and a few red dots in the picture, and the two meshes
  of the fillet were made for nothing. Found by looking at the picture of the control. The remedy is now made only where the check of the part finds a corner short of the radius; otherwise the card says *no change required* and the part is the part. (Walls, gaps, draft, undercut and overhang
  are exact fields and the part's own where nothing is wrong: they are made as before.)
- **`rounded_box(a, b, r)` says what its radius is a fraction of.** The description read "radius (as a 0-1 fraction)": it is a fraction of half the shortest side of the box (0.4 on a box 15 mm thick: a radius of 3 mm). A test that passed 3, meaning millimetres, drew a diamond
  7 mm across where it wanted a box with 3 mm edges, and the tool rightly listed six broken rules for it; found by looking at the picture. The reference (`docs/reference.md`) is regenerated.
- **The check's report said "1 region" for three thin plates 14 mm apart**, and put its `at` in the air between them: the flagged samples were joined on a grid of cells of the size of the scene, and `at` was the centre of all of them. The regions are now found by single linkage (a few times the
  median distance between the flagged samples, at least the limit of the rule), and `at` is the flagged sample nearest the centre of its region.
- **The check could not see a narrow round hole.** A hole of 0.3 mm was never looked at: the check skipped every point where the surface turns by more than a tenth over half the limit's distance, which is every point of a small hole. It skips now only the points near an edge (3 steps of the
  field's own differences, or 12 % of the limit).
- **The colour of a thin layer no longer depends on where the mesh put its vertices.** A wall thickened by 0.15 mm showed blue bands (material cut) on the walls of the holes of a perforated plate although nothing was cut: a vertex of the drawn mesh is a fraction of a cell off the true surface, which on a layer that thin is the
  other side of the old surface. The old part's field is now read at the vertex carried onto the result's surface.
- **`manufacturing_analysis` on a shell's result stood the part on a plate below it.** The result's box is the part's box padded by 5 % (room for what was added); the check took the print plate from it, so the part's own bottom face stood 1.5 mm above the plate and counted as
  a flat roof: 5.9 % of the surface of a ledge, 15 % of a block with a teardrop hole, marked as overhanging, while the shell's own re-check (made on the part's plate) said nothing. The result now carries the plate of the part it was made from, and a check, a second pass and the supports
  read it from there: the ledge reads 0.04 %, which is the strip of a tenth of a millimetre where its gusset ends on the side face.
- **A teardrop cut a pocket of air out of the solid above it.** Over a tunnel with a round roof the overhang remedy (`overhang_fix='cut'`) took away 347.7 mm^3 where the teardrop is 154.5 by hand, and over a spherical cavity 96 where it is 27.4: a second pocket of air floated in the
  solid 3 mm above the apex of each teardrop, and over the cavity it reached the top face. The cause, found with the replay of the disc test at one point: a disc is tested at eight points of its rim and its centre, and the widest disc (18 mm across) had its whole rim outside the small part -- air --
  and its centre in the channel -- air --, so it passed for air all over although it ran through the walls. The wide discs are now tested half way out as well (78 lookups instead of 54): 155.6 and 25.5 mm^3 against 154.5 and 27.4 by hand.
- **The fill of an undercut had a staircase for an edge.** Filling what lies under the lips of a dovetail groove (4 mm at the mouth, 8 at the bottom, 6 deep) left a groove 4.13 to 4.50 mm wide instead of 4, and 95 mm^3 of the 120 by hand: the material over and under each point was looked for at
  16 places counted from the point, so a sliver of lip thinner than the spacing was hit by the points whose count happened to land in it, and the edge of the fill was a sawtooth as high as the spacing times the slope of the face (0.4 mm). The material is now looked for at 17 fixed heights along the direction, the same for every
  point -- the first and the last just inside the ends of the part, shared by the readings up and down, which is half the lookups --: the groove is 4.02 mm wide, 118 mm^3 are added, nothing is left to mark. A shelf thinner than a sixteenth of the part's length may lie between two heights.
- **What the check says of a corner's radius.** A corner is marked from about 17 % short of the radius asked (under 2.5 mm for 3 mm), not from the 5 % of the other rules: the check reads how far from the part the centre of the ball sits, and in a corner of 90 degrees that grows by 0.29 mm for every mm of the corner's radius.
  The radius of the largest ball that fits was tried (9 % short) and marked every tight arc whose normal was a degree or two off, 1 % of the surface of a fillet read through a mesh, so it was not kept; the limit is in the docs and in KNOWN_ISSUES.md.
- **The card of the supports was a bar from -1 to 1 for a body of one colour.** The block of support was drawn with a constant on the numeric scale, so its card showed a rainbow bar and the hover read "Supports: 0.15". It is one amber line now -- a swatch, the word and the close button -- and nothing is read
  under the cursor.
- **The words on the result cards were never translated.** "added", "unchanged", "removed" (geometry change) and "broken", "kept" (a rule check) were drawn from a table of plain strings that the translation tool does not read, so the card was English in all six languages. They are
  `T("...")` calls now, and translated (1,388 of 1,388 texts in every language, with the texts of the menu tables).
- **A part left as it is, with supports under it, is no longer listed as still broken.** When the gusset was refused and a block of support was made instead (`support='block'`), the card said
  *Places still breaking a rule: 1 (11 % of the surface)* for the very ledge the block holds up, and the report listed it as *no remedy available*: the shell re-checked a result only when it had changed
  something, and read the part's own check, without the supports, when it had not. A result with supports is now re-checked with them, as one that was changed always was; what is still listed is what is
  not held.
- **The face of a support block is flat.** The end face of a block (the one that stands where the part above it ends) was drawn in fine terraces, a different distance in from the part's edge at every
  height: how far up the part starts was found by drawing a line between two samples of the field, which is exact over a face and wrong beside an edge, where the field goes flat inside the part.
  The distance is now walked to from the air side (never nearer than the air before it is wide, then three steps of the field's value), exact beside edges too.
- **A supported place is held up to the last half millimetre of the support's edge, and a leaning roof is held as well as its crown.** The check looked for the support straight under a place, `support_gap` down;
  the support's top is that far from the part *along the normal*, which on a face that leans is gap / cos further down (0.42 mm at 45 degrees), and a support stops a few steps short of the edge of what it holds.
  Both made a ledge or an arch that a block held whole show "2 places, 3 % of the surface" on the card. The check now looks at the right depth and a little to each side (4 + 2 steps, one constant in `manufacturing_rules.py`).
- **Closing the program after a script error no longer ends in a crash** (Windows exit code 0xC0000374, a corrupted heap). With any error in the script -- also a syntax error -- closing the window crashed it. At exit the
  window's children are destroyed in the order they were made, the viewport and its model tree before the editor; the editor's document still sends changes of its text while its syntax highlighter lets go of it, and the
  window's three error handlers answered by telling the viewport and the tree "no error": a write into objects that were gone. They now hold the viewport, the tree and the timer by guards and do nothing once they are
  gone. (Found by validating the heap at each step of the shutdown; the warning sign character U+26A0 and the banner's drawing, first suspected, were not the cause: the banner now paints its own triangle and the
  tree's note has no symbol all the same.)
- **The cut remedy left a sheet inside the material where the result's field touched 0.** The part's own bottom face rests on the plate, and a disc point of the cut remedy that landed exactly on it read 0 from both sides:
  a dotted line across the section view and degenerate triangles for the mesher, one reach and an inset over the plate. The material under the plate now counts from four steps above it. A faint hairline is left
  in two short stretches of that section (the field has a crease at that height, 0.14 mm from zero, and the section draws it; the surface of the part, seen from outside, is clean): `KNOWN_ISSUES.md`.
- **The bridge rule no longer marks spans under its limit.** The chord of air under a roof was read at ten places along each ray, up to four millimetres apart out where they are wide, and the wall taken at the first one
  inside the part: an 18 mm roof was read as 23 mm and marked against a limit of 20. The wall is now found between that place and the one before it, by bisection (four rounds), in the colours and in the report, and
  the rays are read at twenty places.
- **"Material added" is the volume of the union of what the remedies add, not the sum of what each adds alone.** Where two remedies work on the same place -- a fillet at the root of a thickened wall, a draft wedge and
  an undercut fill on one wall -- the sum counted that place twice: the card of the moulding sampler (a plate with a fin, a slot, an L, a boss and a hole) said 1,516 mm^3 where 1,180 mm^3 were measured on the field.
  Each remedy after the first is now measured for what it adds that the ones before it do not, on the points it was measured on, and `.added`, `.share` and the card say that volume; each line of `.changes` is still what
  that remedy adds on its own, and the budget (`max_change`) still adds those, the safe side.

### Manufacturing: constraints, a check of them, and a shell that changes the part until it keeps them
- **The volumes of a report are measured twice.** The first points, over the whole box, say where a remedy changes anything; a box round each of those places is then measured again on 30,000 points. A teardrop of 107 mm^3 in a box of hundreds of thousands was three points, and the report said 138.7 mm^3; it says 110.6 now (and the gusset 4,905 against 5,083 by hand).
  The check's own entry function is private (`_analyse`: it was a public name that every script saw), the library reference lists the three manufacturing modules, and the 71 texts of manufacturing and parametric optimization are translated into all six languages (1,388 of 1,388 texts at the end of this round, 0 missing).
- **The check is independent of the shell, so that it can verify it.** The code is in four files -- `manufacturing_rules.py` (the constraints and the presets: what is asked), `manufacturing_field.py` (how a part's field is read), `manufacturing_check.py`
  (`manufacturing_analysis`) and `manufacturing.py` (`manufacturing_optimization`). The check imports nothing of the optimization; the optimization calls only the one entry function of the check (`_analyse`, private), to be told what its result still breaks. Where the report measures a wall or a
  gap it takes the shortest of nine lines through the point (the normal and eight leaning 20 degrees away), where the remedies find a thin wall by the line along the normal alone; the face a line ends on has to face back at the point (within about 37 degrees), so that the foot of a wall on a plate or the edge of a block is no thin wall and no gap.
- **Engineering > Manufacturing** (a category of its own, green): **constraints** as inputs, like a support or a load -- `manufacturing_constraint` with every rule optional (`overhang`, `min_wall`, `min_gap`, `max_bridge`, `min_radius`,
  `draft`, `undercut_along`, a `region=`, a `build_direction` that can be left free) and the presets `fff_constraint`, `cnc_constraint` and `molding_constraint` with the numbers of a process (from published guides, not a standard); the
  overhang rule is off in the presets (slicers make supports) and is for what supports cannot reach, scoped with a region. **`manufacturing_analysis`** is the check: every rule is a **field** on the part, drawn as colours on it (the result card
  switches the rule), and the report says how much of the surface breaks each rule, how badly and where. **`manufacturing_optimization`** is the shell: it **changes the geometry** -- thin walls thickened (an offset that follows how thin the wall
  is), narrow gaps closed or widened, inside corners rounded to a radius (the library's `fillet`: a true circular arc), a draft put on the walls, undercuts filled, overhangs made to stand (a gusset at the angle asked, or a teardrop cut above a channel) -- and
  the result is a body, shown as the geometry change (three bodies of exact fields: grey where the part is as it was, red the material added, blue the material cut), with what was added and cut, a **budget** (`max_change`: a remedy that would change more of the part is not made, and says so), the regions that are excluded or kept left alone,
  and what is still broken, where. `.oriented()` turns the part so that the way up of the print is z, and an export writes it turned.
- **How it is made.** The check and the remedies read the part's own field: normals from the field's derivative, the thickness along the normal found where the field crosses 0, what a ball reaches, shifted copies of the field -- nothing is meshed (the remedy that rounds inside corners is the library's `fillet`, which reads the field too: see "Exact distances ... read the field itself" above). The
  numbers of a report (how much of the surface, where) are **measured** on a few thousand points of the surface, found by projecting random points onto it, and are worked out a second way (plain arithmetic on the values of the field along the lines
  each rule looks along), so the colours and the numbers are made two ways. **Verified on known answers**: 8 check cases (a 0.6 mm fin against a 0.8 mm wall, a 0.3 mm gap, an inside corner, a ledge, a vertical wall, a groove, bridges of 30 and 10 mm
  with 20 asked) and 8 shell cases (the fin thickened and the pillar left alone, a gusset under a ledge, a groove filled, a draft of 2 degrees, a gap closed, an inside corner rounded to its radius, a teardrop above a hole -- 104.7 mm^3 cut where the
  geometry says about 108). A check of the part's own field is not a substitute for the check of the one who makes it.
- **Supports and the files of a print.** `support='block'` on a constraint (with the overhang rule) lets the shell add, under every surface of its result that still leans out more than asked, a **block straight under it** (to the plate or the
  part below, `support_gap` from the part), as a separate body: `.supports`, `.support_volume`, and `no_support=` regions where none may be made. `export_print(result, "part.3mf")` writes the part (turned into the way up of its print), its supports (type
  `support`) and the no-support volumes (type `other`) as separate objects of one 3MF; `export_layers(part, "layers.dxf", layer_height=0.2)` writes the contours of each layer as closed polylines on a DXF layer each, and `export_slice(part, "slice.svg", z)`
  one layer as a picture -- both read from the body's field, nothing meshed, no G-code. A constraint with a free `build_direction` stands the part the way that leaves the least leaning out (the first 0.6 mm on the plate excepted), the lowest print among ties.
- **Fixed on the way.** `export_mesh` of a body whose extent is exactly its faces (a box) found nothing to write: the region is now one cell larger all round when no `bounds` is given. The surface a ray meets first (`_entry`) missed a face that a sample
  landed on exactly. In the window, a run whose result is a field of shifted copies of shifted copies froze the main thread when the viewport compared it with the shapes it already had (`Tree::cooptimize` walked it as `optimized()` used to): it now
  tells such a tree by its content instead of walking it.
- **Why the result took minutes to draw, and what it takes now.** A remedy asks the part's field for its value at other places (how far to the far side of a wall, what is above a point), and the mesher then works through those lookups for every cell. Two
  causes were found by measuring, not guessed: (1) **exact ties in a `min` or `max`** -- looking at a part from where the lookup does not move what it sees (straight up at a vertical wall) gives the same value over a whole region, and the mesher, placing a
  vertex, tries every way such a tie could be settled: 24 such lookups took 53 s to mesh and 0.4 s when each was a few microns apart in value, so every place that combines lookups, and every remedy united with the part, now keeps them a few microns apart
  (`_join`; the walls remedy alone: 115 s to 1.8 s); (2) **too many lookups**: the overhang remedy used 625 (a 24-sided disc at 25 depths) and now uses 78 (8 sides, 6 depths close together near the surface, and a second ring half way out on the wide discs, which a rim of eight points alone lets pass through a wall) -- the same result on the example to a tenth
  of a cubic millimetre, in a quarter of the time -- and is limited to where there is part above the point, as the thin-wall and gap remedies are to where the surface is near. Example 33 draws in 18 s at resolution 3 on the final build (it was 133 s when this was first measured, and it never finished before).
- **Kernel: a field made of fields made of shifted copies no longer costs a multiple of itself at every level.** `Tree::optimized()` walked a graph once for every way to reach a node, so nesting copies (smooth, the manufacturing remedies, every remap
  inside another) multiplied the cost by about five for each level: 0.85 to 3.5 s for a chain of seven. It now counts the ways first (one pass over the nodes) and leaves a tree that has hundreds of ways to each node as it is (a part imported from a file has
  about one, and is optimized as before): 0.005 s, whatever the depth, and the values are the same. `Tree::flatten()` remembers what a shared node comes to under a substitution. The check of a thin wall is 0.01 s to set up instead of 0.9 s, and the shell
  on a part with a thin fin takes 2.4 s instead of 69 s. Example `33_manufacturing.py`; docs `manufacturing.md`; six translations.

### Parametric optimization: the best numbers for the var()s of a model, over any analysis
- **`parametric_optimization(analysis, [(var, low, high), ...], minimize= | maximize=, constraints=)`** (in the Engineering menu, directly: it works over an analysis of any domain) finds the numbers of the
  `var()`s that make a measure of an analysis as small (or large) as it can be while the constraints hold -- the lightest beam under 90 MPa that bends less than 0.5 mm, the stiffest bar of a given mass, the coolest slab.
  Each design the search tries is the analysis solved again with other numbers in the `var()`s (the model, the mesh and the solve: static, modal, thermal or flow), kept in the result cache like any other; the
  search needs no derivatives (a spread of designs, then surrogates of the objective and of each constraint pick the next design, closing in on a bound or a limit), a design that keeps the constraints always
  comes before one that does not, and a design the mesh cannot hold is listed as failed. The measure is the name of a number of the result (`'mass'`, `'max_von_mises'`, `'frequencies[0]'` ...), an expression of
  them or a function of the result; a constraint is a comparison in a text (`'max_von_mises <= 90'`) or a function. The result is the analysis of the best design (its fields, its numbers, the result card, the exports) with
  `.values`, `.objective`, `.feasible` (when no design keeps the constraints it says so and is the one that breaks them least) and `.history`, every design tried -- the output pane lists them all.
  `apply=True` writes the best numbers into the text. Measured: the lightest cantilever (width and height): 36 designs, 4.3 s, 8 x 14.3 mm, the displacement limit decides (beam theory: 14.26 mm), 5 % lighter
  than the best of a grid of 156 designs (63 s); the stiffest bar of a given mass finds the square section (12.2 mm, beam theory 12.17). `Result.mass` (kg) and `ModalResult.volume`, `.mass` are new for
  the objectives; a result that stands for a design is exported as its analysis. Example `32_parametric_optimization.py`; the Engineering menu entry needs a result selected.

### Engineering: a menu of its own, with a category for each domain
- **The Simulation menu left Operations: right-click has an Engineering menu of its own** (beside Primitives, Parametric shapes and Operations, on a model and on empty space), with a category for each domain --
  **Structural** (supports and loads; *static_analysis*, *modal_analysis*, *topology_optimization*), **Thermal** (thermal conditions; *thermal_analysis*, *thermal_topology_optimization*) and **Fluid**
  (flow conditions and fluids; *fluid_analysis*, *flow_topology_optimization*) -- and the materials, which an analysis of any domain takes, under them. A category shows only when it has a valid entry, as every
  category does. **Every domain has its own colour and icon**, in the menus and in the model tree: a simulation in the full colour (structural red with bars, thermal orange with a thermometer, fluid blue with
  streamlines), and what an analysis is given in a lighter tint of the same colour with a picture of its own (a load on a support, heat rising, a flow into a channel), so what feeds which analysis shows at a
  glance. A model's `domain` comes with its row (`fieldes/kinds.py`: `DOMAINS`, `DOMAIN_OF_FUNCTION`, one list for the menus, the tree and the icons); the Engineering menu has a truss as its icon. There is no
  Operations > Simulation any more (a beta change: no alias). The in-app guide, the docs, the README and the six translations say it.

### 10-node tetrahedra: `element='tet10'`
- **A quadratic element for `static_analysis` and `modal_analysis`: `element='tet10'`.** The same mesh with a node in the middle of every edge: the displacement is a quadratic in each element, the stress is linear in it, and the edges on the surface
  are bent onto it (a midside node is moved along the faces' normals to where the part's field is zero; an element that would be turned inside out keeps it straight). **Accuracy**: the cantilever beam reads -0.8 % with 1,296 elements where 66,816 linear ones
  read -2.0 %; the plate with a hole +0.4 % with 45,000 elements (13 s) where 963,000 linear ones read -3.7 % (22 s); a round bar bent by a tip load +0.1 % with 4,456 elements; the first bending mode of the beam 813 Hz with 1,296 elements (converged: 812) where 66,816 linear
  ones read 818. A patch of uniform stress, a bar under its weight, a bar heated between two walls and a graded stiffness are exact or agree with the linear element. **Cost**: on the same mesh it is 6 to 7 times the unknowns, 14 times the stiffness matrix and up to twice the
  iterations (15 to 45 times the time; the beam: 0.8 s against 35 s), so it is **its own element, not the default**: it is for the places where the stress matters and the part's thin features do not set the element size. A mesh too coarse for the feature reads **high** (1 mm on the
  plate's hole: +20 %): halve the size and see whether the peak moves. Everything a static analysis has works with it -- loads (a force's profile, gravity, thermal expansion), supports, fields as the material, the result cache across sessions, the result card (peak at the nodes),
  `colored(...)`, probing and the fields as shapes -- and the result is drawn on each element cut into eight linear tetrahedra. Not for it: topology optimisation, thermal and flow analysis (each says why), and `export_fea` (the model another solver would read; `export_results` works).
  The default element size is the part's longest side over 30 (over 60 for `'tet'`); a mesh too fine for the free memory is refused with the figures. `examples/verification/v1` and `v2` check it against the same answers, and `docs/analysis.md` has the measured table.

### Four known answers, and a notice
- **`examples/verification/` has four scripts that solve a problem with a known answer and stop with an error when FielDes is farther from it than a tolerance**: a cantilever beam against Timoshenko beam theory (−2.0 %), a plate with a hole against Peterson's stress concentration (−3.7 %, one eighth of the plate by symmetry),
  Poiseuille flow between plates (−0.3 % on the peak speed, +0.2 % on the pressure gradient) and conduction through a bar against Fourier's law (0.14 K, 0.5 %). They never use the result cache, so they always solve; run them after a change to the solvers, the elements or the caches. They are in
  [docs/analysis.md](docs/analysis.md#verification-four-known-answers) with the numbers as measured today. **The result card now says *Not validated for safety-critical use*** (its tooltip says why and where the checks are), and so does the README. Four cases do not make a validation, and the text says so.

### Settings ▸ Navigation: an option beside the default
- **Two more sets of mouse gestures for other hands, as an option.** *Blender style* adds **Shift + the middle button** to pan; *Touchpad* adds **Alt + left drag** to pan, for a laptop that has no right or middle button to spare. **The default is not changed and stays the default**
  (*FielDes*: Shift + left drag or the middle button turns, right drag pans, the wheel zooms); the others only add to it, so nothing it does stops working. The choice is kept in the settings.

### Report a bug, and the known issues
- **Help ▸ Report a bug...** opens a new issue on GitHub in the browser, filled in with the version of FielDes (and the kernel revision), the system, the Qt version and the graphics driver -- what a bug needs to be found. Nothing is sent from the program: the issue is read and
  submitted on GitHub. The script is not in it unless you choose *Open it and copy my script*, which puts it on the clipboard to paste (a script is too long for a link). **`KNOWN_ISSUES.md`** lists what does not work as it should yet (the round-corner fill is a mesh, splines are
  measured in pieces, the coarse picture while a ring is dragged, the unsigned Windows setup, the platforms).

### Where the keyboard is
- **The pane that has the keyboard has a thin blue frame, and the status bar says *Keys: viewport* or *Keys: editor*.** The viewport's single keys (`W`, `E`, `D`, `R`, `I`, `V`, `C` ... and `Shift+W`, `Shift+S`, `Ctrl+D`) are letters in the script editor, and nothing on the screen said which one
  had the keyboard -- a key that did nothing, or typed a letter into the script, was the only answer. The frame follows the keyboard (a click, `Ctrl+Shift+W`, a tour step that gives it to the viewport) and is gone while a menu, a dialog or another program has it; the model tree
  belongs to the viewport, the output pane to the editor. Hovering the label lists the viewport's keys **as they are bound now**: the list is read from the actions the viewport owns, so a key that is changed in *Keyboard shortcuts* or added later is in it. It is a small `FocusCue` widget
  (`app/src/focuscue.cpp`) that any pane can be added to; the tour's *Keys worth knowing* step says it.
- **Turning the view says that it closed the work plane view.** Shift + left drag (and the middle button) turns the camera, and the work plane view -- the plane is parallel to the view it was opened in -- is left; the grid and the drawing were gone without a word. The status bar
  now says it and how to come back (*Shift+W opens it again, and W turns the view to look straight at the plane*), with the keys as they are bound now.

### The guided tour, shorter and in one piece
- **The tour is one walk of 32 steps after the welcome card (it was 40), and a tour that is skipped says where it is found again.** Steps that said one thing in two cards are one: *the model tree* and *selecting a model*; *the arguments of a call* (the rows under a model are all its arguments: the card that listed the children and the card about the argument rows said it twice); *a placeholder*, explained, made from the right-click menu and filled in (one card asks for both); *stopping the drawing with a right click* and *coming back with `E`*; *the sweep* and *playing with it*; the corner step and *the gizmo of a point*; the mirror and *the first thing made with it*; *editing the original* and *editing through an image*. **Gone**: the card about the arrows that open and close the rows of the tree (they are known from every program that has them), and the card that said lines and sweeps are mirrored too (it is in the hint of *On and off*). **The order**: *Hold Shift* comes right after the gizmo (it is the same tool), and *Keys worth knowing* is the last step before the end, where it is about what the steps before have shown. **Nothing is hidden behind a setting**: the work plane, the symmetry and the rest are in the one walk, and *Skip the tour* (on any card, the welcome card too) ends with a card that frames *Help ▸ Guided tour* in the menu. **A new step, *A lattice that follows a field***, comes after *Move the point*: a block filled with a gyroid, drawn at resolution 7 so that the walls are smooth, whose wall thickness is a field (`walls`, a ramp thin at the left and thick at the right); changing one number in its line changes every cell.

### A sketch is a profile
- **`sketch` is now `profile`.** A name says what a thing is, not how it is made: the 2D shape that a closed outline through points encloses -- corners, spline points and round corners -- is a *profile* (the word nTop and CAD programs use for
  the extrudable region of an outline; the drawing and its constraints, which FielDes does not have, are what CAD calls a sketch). `profile(points)`, `profile(surface)`, the *New 2D shape ▸ profile* entry, the models `profile_1`, the
  `Profile` and `SurfaceProfile` classes, the module `fieldes.stdlib.profile`, `docs/profiles.md` and the examples `26_profiles_and_sweeps.py` and `28_profile_on_a_plane.py` follow, and the guide, the tour and the six
  translations say it. **The old name is gone, not aliased** (a beta change: a script from before needs `sketch(` changed to `profile(`; a variable called `profile` must be renamed, or it hides the function). `extrude`, `path_extrude`, `handles` and
  `gizmo` keep their names. The work plane is described as what it is -- a pen tool on a plane, as in a vector drawing program -- and the docs gain a short list of the words that mean something particular here (gizmo, handles, profile, copy,
  linked copy, images, shadow, field).

### Round corners: a point with a radius
- **A point of a profile can be a round corner: `point(x, y, z, radius=4)`.** An exact circular arc of that radius, tangent to both edges, takes the place of the corner; the point stays where the two edges would meet, so it is still the
  one you drag. It is a corner with a radius (`radius=0` is a hard corner again), `profile(points, radius=5)` rounds every corner that does not say otherwise, and a spline point has no radius (`point(..., smooth=True, radius=4)` is
  an error that says why). **The arc is exact in the field** -- the distance is measured to the circle, not to a polygon -- so `extrude`, `offset`, `shell` and a simulation see a true circle at any resolution; the viewport draws the face from
  the numbers of its points while you drag, as before. **The blue outline is drawn from the curves, not from pieces**: the arc of a round corner (and the cubic of a spline) is drawn as the curve itself, cut on the screen
  to a twentieth of a pixel at the zoom you have, so zooming in never shows a corner on it, and a press is looked for on that curve too. (The filled face is a mesh of triangles and is cut into pieces by its nature.) **The radius is limited by the geometry**: the two arcs on one edge may take no more than the edge is long, so a square asked for round corners of half its side (or more) is
  a circle, and a rectangle asked for more than half its short side is a stadium. **The arc is the handle of the radius**: hover it and drag -- pulled in it is rounder, pushed out towards the corner it is sharper, up to what the geometry
  allows -- and the number the point says is written as you drag (`radius=var(5)`; a plain `radius=5` becomes a `var()` the first time the arc is pressed). An arc is never an edge to pull a point out of: that broke the corner.
  The point's own gizmo is there too: its scale ring multiplies the radius, its rotation ring is not shown (an arc has no direction), moving it moves the corner. **A double click on a point goes corner, spline point, round corner**, and back to a corner (a new round corner starts at a quarter of its shorter edge); the dot of a round corner is a square with
  round corners. (The distance to a spline is still measured to straight pieces by the kernel, 14 to a segment -- a cubic has no closed form for its distance -- and on a line or the path of a sweep a round corner is a hard corner.)

### 2D shapes on top, a symmetry that is a model, linked handles, a tour with a state per step
- **The guided tour has a symmetry section** (six steps, after *Copy or link*): a mirror is a model and the one that is selected is in use; making something puts its images under it; editing the original, and editing it through an image; Shift+S; lines and sweeps. Each works on a table of its own, has a *Show me*, and undoes itself -- the tour now saves and restores the symmetry in use with the rest of a step's state.
- **The symmetry is a model, in 3D.** `mirror = symmetry_planes(planes=2)` is a model of the script like a point or a surface -- a row in the model tree with an icon of its own, a gizmo that moves and turns it, its planes drawn in the
  viewport. **Select it and it is the symmetry in use: everything you make from then on is made at its images too** -- a body, a point, a surface, a field, and a profile, a line or a spline (through the images of its points; half
  a shape whose ends are on a plane is closed through its image into one profile). **All the images of a model are ONE model**, written under it: `box_1_images = symmetry_images(box_1, mirror)  # shadow: box_1, mirror` -- one row in the tree and one line in the script, however many images
  the planes make (47 for a cube), and the original is the only one with numbers. Each image is still drawn, meshed and moved as a shape of its own, so dragging the original moves all its images as it is dragged, with nothing
  meshed again; select the row and all of them are selected. **The images follow both the model and the symmetry**: change the model and they change as a mirror shows it; move the symmetry with its gizmo and every image of
  it moves. **They have no numbers of their own: drag the gizmo shown at the first image, or the surfaces of any of them, and the original moves** -- mirrored (an arrow moves it the opposite way across a plane, a ring turns it the
  opposite way). An image that would be the model itself or another image (a ball with its centre on a plane) is not made (`only=(1, 3)` lists the ones that are); the images of a model that is deleted are deleted with it.
  A profile, a line or a spline is made of its points, which are models: each point has its images (`symmetry_image(point, mirror, 3)`) and the stroke goes through them; half a shape whose ends are on
  a plane is closed through its image into one profile. **What an operation makes gets its images as well**: a
  `path_extrude`, a union, a shell, a move ... of bodies or 2D shapes is written `path_extrude_1_images = symmetry_images(path_extrude_1, mirror, distinct=True)` -- the images that are not the model itself
  or another image, told when it runs (a tube along a path in a plane of the symmetry has none in that plane); what is made of images already is not given more. A surface or a field has a model for
  each image (`symmetry_image(model, mirror, 3)`). **The images of a `path_extrude` are drawn like it is: natively, from the numbers of the path, so they follow a dragged point at once (no minute-long meshing), and their paths and points are drawn where its sweep is, on the image tubes.** **What a model was made from is mirrored with it, whatever the operation**: each statement `name = f(...)` notes the models it read (`made_from.py`: nothing of it is in `f`), and the images of a model bring the profiles, lines and points of everything it was made from -- the profile and the path of a `path_extrude`, the profile of an `extrude`, through a chain of offsets, unions and gizmos -- as a copy of the outline or the line and a dot for each point. No models, no rows in the tree (the path and the profile are children of the operation and the only ones with numbers); a dot of any image's is dragged and it is the original's point that moves (a double click changes the original point between a corner and a spline point), and when the number of copies is changed the images follow (the image paths used to be rows of their own, written when the path was drawn, that stayed where they were when the count changed). **Changing the copies or the planes of a symmetry on the card now redraws what was made with it**: the saved result of a statement such as `leg_images = symmetry_images(leg, mirror)` was handed back when only the copies had changed (a symmetry's expression is a small ball, whatever its copies), so the legs stayed while the card said 5. And `distinct=True` of a sweep is told by the numbers it is drawn from, not by sampling its field (seconds to half a minute in the application, and an error): instant. **Pulling an image's surface changes the model it is the image of, never the symmetry**. Several symmetries can be in the script; `Shift+S`
  uses the selected one or stops (with none in the script it makes one: on the work plane while its view is open, else the mirror x = 0). Three arrangements: `kind='axis'` (1 to 8 planes through one axis), `'across'` (and the plane
  across it), `'cube'` (the nine planes of a cube) -- up to 47 images. The card (top right) changes the selected symmetry: arrangement, count, axis (x, y, z, the work plane's normal, up in the view, across the selected 2D shape), turn,
  centre; the count and arrangement are written into the call, the place into the numbers of the gizmo. **Select the images of a model and the symmetry they were made in is the one in use**: its planes are drawn, its card shows, and what you make next is made at its images too -- the model shows how it was made, ready to be added to.
  The old symmetry of the work plane's card (a row of numbers, images that were shapes of their own, handles in the viewport) is gone. New example `29_symmetry.py`.
- **The Shift gizmo turns in steps of 5 degrees.** With Shift held, the rotation rings snap: the angle a part ends at is a multiple of 5 (from a part that stood at 3 degrees too), and a group of points keeps the differences
  between its points' angles, each turned by a multiple of 5. The angle shown beside the ring is the snapped one. (Checked in a real window: seven ring drags with Shift, each ended on a multiple of 5; without Shift the angle is free.)
- **Two new examples to try the noise and the textures:** `30_noise_kinds.py` (every kind of `noise_field`, stretch, warp, octaves, a scale that is a field) and `31_texture.py` (every pattern of `texture`, a
  region by surface or by body, `side`, a pattern and a depth of your own).
- **N-way rotational symmetry: `symmetry_planes(copies=3, kind='turn')`.** A symmetry of copies equally spaced round an axis, each the one before it turned by 360 / copies degrees, nothing mirrored: three legs of a table, five blades, seven
  teeth -- any number from 1 to 24 (the planes of the other kinds stay 1 to 8: two planes at right angles make four copies, two of them mirror images; `copies=4` makes four turned ones). The card has a *Turns* kind with a **Copies** row
  (1 to 8 and a number box up to 24); the viewport draws a disc across the axis with a spoke where each copy's sector begins; changing the kind on the card moves the number between `planes=` and `copies=` in the call (the card can now take
  an argument out of a call as well as put one in), and an argument it adds goes on a line of its own. The card also has buttons for 5 and 7 planes, and is wider.
- **The symmetry snaps to the work plane's grid.** While the work plane's view is open and its grid snaps, the centre of a symmetry lands on the grid when it is moved by an arrow along one of the plane's axes (its dot and its plane squares did
  already).
- **A solid made from a profile -- an `extrude` -- meshed from its field is no longer torn when it is turned.** A quarter turn of an extrusion (by its gizmo, or as an image in a four-way symmetry) was meshed with teeth along its walls and pieces missing at a corner: the profile's field was made in coordinates measured from its first point (`x - 12`), the turned one in the mesher gave +11 % of surface that the solid does not have, and the same field made from the origin of space (`x`) is meshed exactly (measured on the mesh: 0.0 % against the exact surface at every resolution, for every corner the outline can start at, and in a tilted plane). A box was never affected.
- **A 2D shape turned out of its plane is drawn as its tilted face.** A rectangle, a circle or a triangle turned about x or y by its gizmo, or moved along z, was drawn as the slice of the turned shape at z = 0 -- squashed, and gone at a quarter turn: the plane it is drawn in now turns and moves with it.
- **The turn matrices of a symmetry are exact** (`cos 90` was 6e-17, not 0): an image of a shape whose faces lie on whole numbers has its faces on whole numbers too.
- **A symmetry's gizmo is along its own axes.** `Shift+S` on the work plane makes the symmetry perpendicular to the plane (its axis the plane's normal, its first plane across the plane, centred on its origin) -- and its gizmo is
  now turned to fit: arrows, plane squares and rings along the plane's u, v and normal, however the camera was turned when the plane was made (it used to keep the world's axes, which left a symmetry on a tilted plane
  impossible to move or turn along the plane). A symmetry along the world's axes keeps the world's gizmo, and the gizmo follows the symmetry when it is turned.
- **The symmetry key is `Shift+S`**, as `Shift+W` opens the work plane (`Ctrl+Shift+S` stays *Save as*): it uses the selected symmetry, or stops using it.
- **A new file is empty.** FielDes no longer starts with a sphere in the script (File → New, *Load the default script* and the first start): nothing is made for you to delete first.
- **`Ctrl+Shift+W` works when FielDes has just opened.** It was the viewport's shortcut, and the keyboard is in the code editor at the start, so it did nothing until the viewport was clicked. It is the window's now (it types
  nothing, so it needs no focus), and it gives the keyboard to the viewport: the keys of the drawing (`E`, `Shift+S`, a right click) work at once. The single keys (`Shift+W`, `W`, `Shift+S`) stay the viewport's, as in the editor they are letters.
- **Undo and redo: the lines the program writes by itself are not steps of their own.** A new model gets its `gizmo(...)` / `handles(...)` lines, and the render region follows it, in edits of the program's own
  after the user's: *Ctrl+Z* only took those back, they were written again at once (the model was still selected) and the model never went -- a point placed with the work plane's Point tool, a new sphere, any new
  model. Now one undo takes the program's own edits together with the edit of the user's before them, and one redo brings them all back (checked: a new sphere goes with its lines and its region and comes back, a placed
  point the same; deleting a model and undoing it is still one step).
- **2D shapes are drawn over the 3D ones, in a very light blue, and picked first.** A profile or a flat shape drawn on a body (or inside it) used to hide behind the body's surface or hide what was drawn there; now it is
  always seen whole, in front of everything 3D (they are in front of each other as their places say), and a click on it selects it, not the body under it. Its blue outline can always be pressed.
- **The lines of the work plane's symmetry are a thing to snap to.** With *Snap to points* on, a point put down (or dragged) near a symmetry line lands **on** it -- along the line, on the grid's steps from the centre while the plane
  snaps -- so what is drawn on both sides of a line meets on it. **`Shift+S` turns the symmetry off and on** (it brings back the number of axes there was). **A shape that is closed through the symmetry is made into one**: half a circle drawn
  from one point of the line to another and its mirror image on the other side are joined where they meet and written as **one closed profile** -- **at once, with the click that puts the last point on the line** (as clicking the first point closes a shape), not when the stroke is ended by hand; a line that meets its image at one end, and is ended by hand, is one line. The dot and the knob of the symmetry
  are there to be dragged **only in the nothing mode**; while a kind of drawing is on they are drawn faintly and do not take the click, so a point can be put at the centre.
- **A linked copy has handles, and they are linked to its original's.** Dragging a surface of an `instance(...)` copy pulls the numbers of the original's `handles(...)`: the original and every copy move together, whichever is pulled. The original is
  made ready (given its `handles(...)` line) when a copy is selected.
- **Several points selected are scaled and turned one by one.** The gold ring scales every point by its own scale and the ring in the plane turns every point by its own rotation; the points **stay where they are** (the rings used to scale and
  turn their whole arrangement about its middle).
- **Deleting a profile deletes its points.** A profile (or a `polyline` / `spline`) made from points takes the points it was made from with it, in the same undo step -- they would stay as dots with nothing to do. A point
  that anything else still uses (a second profile, a move, ...) stays; deleting a single point still only takes it out of the profile.
- **The right-click menu is made of what is valid, the same wherever it is opened.** The *New ...* categories (and *Import model...*) were only in the menu of a right-click on a model's surface in the viewport -- from the
  model's row in the tree, or its line in the script, they were missing, and with them *New 2D shape → profile* for a selected surface. Now **a category is in the menu when there is a valid entry in it and left out when there
  is none** (not greyed out), at every depth and for every kind of menu: it is worked out from the entries, not from a list of categories, so a new entry or category is covered without a rule of its own. For that the check
  of an entry that needs a second model now tests the pair it would be written with (the selected model and the last other model of the script) -- before, it called such an entry valid whenever the script had another
  model, so *Extruding* was in the menu of every model. In the library of the menus (`menu_catalog.py`) what an entry is made of is data, not a branch for its name: the ways a *profile*, *polyline* or *spline* is made of
  the selected points, curve or surface are rows in one table (`FROM_SELECTION`), the simulations are the entries of their group and what they take (their parts, their conditions, a fluid or a material) is read from
  their signatures in the library. (Checked against 135,660 selections of every kind: nothing the menus write or accept changed.)
- **`texture(body, pattern, depth, size, region, ...)`: a texture on a surface.** Sandpaper (`'grit'`), orange peel (`'stipple'`), leather, ribs, an egg carton of dimples and a diamond knurl -- or a field of your own -- laid on
  all of a body or only where a selected surface says (`region=top`), fading out over `fade` mm, cutting in or adding (`side`). It is the body's field moved by a height, so it needs no mapping, has no seam and works on any shape;
  a 0.8 mm grain on a 40 mm face renders in about 3 seconds. In the menu: *Operations → Texturing*, with the surface that is selected as its region.
- **Noise has five kinds, a stretch and a warp.** `noise_field(scale, octaves, seed, gain, lacunarity, amplitude, kind, warp)`: `kind` is `'perlin'` (as before, to the last digit), `'billow'` (puffy humps), `'ridged'`
  (sharp ridges), `'cellular'` (Worley noise: the distance to the nearest of random points -- bubbles, pits, scales) and `'cracks'` (the distance from the walls between those cells: a web, a foam's walls); the new kinds are
  laid over about -1 .. 1 (Perlin with four octaves stays mostly inside ±0.3, which is now said in its help). `scale` can be three sizes -- `(40, 4, 4)` is streaks along x -- and `warp` pushes the coordinates about by three other
  noises, so the pattern bends (marble, flowing organic shapes). The cellular noise is new in the kernel (`libfive_field_cellular`); the rest is field math. Checked: bounded, deterministic, continuous, a seed makes a different
  pattern, and the plain noise is identical to what it was. `noise_field` is a primitive of the right-click menu.
- **The right-click menu has three parts -- *Primitives*, *Parametric shapes* and *Operations* -- and a model's menu is 9 lines, not 17.** *Primitives* hold the most basic, low-level representation of each kind, each under its kind
  (*New 3D shape* ...): a sphere, box, cylinder, cone, torus, pyramid; a circle, rectangle, triangle, polygon, profile; a polyline, spline; a point; a plane, sphere surface, cylinder surface; a distance, radial, z and noise field.
  ***Parametric shapes*** hold the more complicated ones that are made with parameters -- a rounded box, a ring, a wave, *low_level_body*, *low_level_field*, the custom blocks that need no model -- by kind in the same way.
  ***Operations*** are what is done to a model: the operation groups, *Lattice* and *Simulation*. Being a primitive is being the most basic form of its kind, not needing no arguments (`menu_catalog.BASIC`). *Primitives*,
  *Parametric shapes* and *Operations* have icons of their own (a cube, a plate with a hole, a cog), and *center* and *bounding_box* are below the three. The same parts open on empty space, on a model and from its row or its
  line; *Add operation / Add lattice / Add simulation* are *Operations* now. The tour's menu steps, the guide, the docs and the translations (six languages) follow.
- **A right-click on a selected surface opens the surface's menu.** The surface patch lies on the face of its body at the same depth, and the picker took the body (drawn first): the menu was the body's. The patch is pulled
  towards the viewer in the picker as it is in the picture.
- **Each library function says what it takes, and the menus read it.** Everything the menus offered for a surface was valid -- a surface fits "any shape", the default of every operation -- so `fillet`, `smooth`,
  `shell_inside` and the rest sat next to `profile`. Now a function declares the kinds it takes on itself (`fn._takes = takes(...)`, `fieldes.kinds.SHAPES` / `BODIES`), the menu reads that, and the old name-keyed table in
  `menu_catalog.py` is gone: `smooth`, `round_edges`, `fillet`, `shell_inside`, `shell_outside`, `shell_exact`, `lattice`, `random_points`, `surface_from_bodies` and `field_from_body` take a body -- greyed out for a surface, a curve,
  a point or a field, and a category with none valid left (*Rounding*, *Surfaces* ...) is not in the menu -- `extrude` a 2D shape, a foam its points. A function that says nothing takes any shape. (Compared on 135,660
  selections of every kind: only those ten functions changed, only from valid to not valid.)
- **A New entry is valid when it is a placement or when the selection has a use for it.** On a model (in the viewport, from its row or its line) every *New ...* entry used to be valid, so a surface selected showed a
  material, a fluid, a gravity and a cell next to *profile*. Now **a shape -- a 3D, 2D or 1D shape (a line and a profile too), a point, a surface, a field -- is a placement: valid for any selection** (a line or a
  profile is made of the points, curve or surface that are selected when they fit, of new points when not); **a condition is valid when what is selected can say where it acts** (a surface, a field, a body -- not a
  point); and **a material, a fluid, a cell or a load is valid when some function that says what it takes -- a simulation, by its signature, `lattice` -- takes the selection together with it**: for a surface the
  materials, the fluids and the analyses are not valid, and a category with none valid (*New material*, *New fluid*) is not in the menu. Nothing in the rule names an entry; the library functions say what they take.
- **A selected surface is drawn in the colour its key shows, and the ruler is no longer pink.** The patch of a `select_surface` was drawn in the *last* colour of the boundary-condition map (gold, since the map grew to 13 colours) while
  its key said *Selected surface* in magenta; it is magenta on the model now, as the key and the docs say. And the scale ruler at the bottom took its second half's fill from whatever was drawn before it -- the key's pink swatch -- so the
  ruler turned pink whenever a selection was shown; it now has its own (no) fill.
- **A section can be started at the end of the model tree too**: hover under the last row and the + in a circle shows there.
- **Ctrl+drag of a row is always that row**, as a shadow reference: it is dragged at once and becomes the selection, whether it was selected or not (before, a row that was not selected joined the selection, the models were made ready to be
  dragged and the tree rebuilt under the hand, so nothing was dragged).
- **Tour: every step has a state of its own.** What was changed or deleted in a step (a number, rows, a name, a section) is put back when the step is left, so the next step starts whole (deleting rows in *Close what you do not need* left *Cut a
  section* without them); the steps of the work plane and of the sweep put their own model back on every entry. **Step 20 starts zoomed out** (the camera goes to the model's region at once, not after the script has run). **New step before
  *Properties*: *Simulations and optimization*** shows an optimization running step by step and points to the examples; **in *Properties* the simulation in the viewport is no longer dimmed.** The *Keys worth knowing* list has `S`.

### A welcome card, a clearer tour, and four workflow fixes
- **A welcome card instead of a tour that opens by itself.** The first time FielDes starts it says in a few lines what FielDes is (field driven design) and offers the tour: **Start the guided tour** (orange) or the quieter **Skip the guided tour**. *Help → Guided tour* starts the tour at its first step.
- **The render region follows a drag at once.** Pull a gizmo arrow, ring or scale handle -- or a **surface** of a model (the tour's *Drag a surface*) -- past the edge of the region: the model is drawn whole while you drag, and the grown region is written **together with the numbers of the drag, in one run, when you let go** -- there is no second run a few seconds later that clips and then un-clips the model. A pulled surface is meshed again at every move, so the region grows *before* the model is given the new numbers: it is made to hold where the model will be (searched for in its field, so a sphere pulled out on one side holds on the other too) and the point the mouse has the surface at.
- **No hitch at the start of a drag.** The search for where a model is (it chooses the resolution) gave up after 50 ms on shapes it cannot bound -- a part with a hole, say -- and did so again at every render of a drag and every time the region grew: now such a shape is searched for a few milliseconds only until it is another tree, and the search that holds the region to a pulled surface is coarse (two per cent, a few milliseconds) instead of fine.
- **A handle that is pulled shows the shape at once.** New numbers cancelled the render that was running at every move of the mouse, so while the hand kept moving no picture was ever finished -- the number in the script followed the hand and the shape stood still for seconds. New numbers now wait for a render that is at its first (quick) level, and the picture follows at the pace of the mesher.
- **An argument row of a number that is `var(5)` selects the number inside, not the whole call.** Typing over it gave a plain `9` and the number could no longer be dragged; now it gives `var(9)`.
- **A section is made by clicking a small + in a circle.** Hovering in the gap between two rows of the model tree shows the circle in the middle of the tree; a click anywhere else in the gap does nothing, so a stray click no longer makes a section.
- **Copies select the copy.** After *Shift+D* (copy) and *Ctrl+D* (link) only the new models are selected; the originals are not -- also when Shift or Ctrl is still held down when the copy appears (the tree used to add the new row to the old selection, or toggle it, because of the key). Duplicating several models selects all their copies.
- **A linked copy keeps its own position and rotation.** `instance(shape)` follows the *shape* of its original (and its scale) but not where it is or how it is turned: move the original and the copy stays; the copy starts where the original is, with its own `gizmo(...)` numbers.
- **`Q` is gone: a right click stops drawing on the work plane**, and **`E` brings back the kind of drawing that was on before** (line, spline or point), then goes on round the three. (A right-button drag still pans.)
- **Tour:** a step that is done turns every outline, the arrow and the card **green** (yellow means there is still something to do); the *Corner or spline point* step points an arrow at a corner; **the *Gizmo* step can be completed** (it is done when the model has been moved: the numbers of the gizmo line changed -- it had no way to say so) and the *Move the point* step selects its point when it is reached with Back or by a jump (it found nothing to select, so there was no gizmo to drag); the rows that a step drags to (an operation's arguments) stay open; the **placeholder steps** recognise a call written one argument to a line; the number step (*Arguments are rows too*) lets you type over the `r` row at once; steps that change a model leave the viewport lit; "Show me" waits until what it did is drawn before it puts things back; the edge step points an arrow at an edge; the Shift gizmo is shown from the iso view; the keys are grouped (general first); the properties step frames the beam; the *Render settings* step is gone. New steps: **stopping with a right click and coming back with `E`**, and **lines through points, a sweep along a line (`path_extrude`) and a sweep to play with**.

### FielDes apps: a script that anyone can run
- **An engineer's script becomes a program for other people.** The script declares what the person who runs it may choose -- `app.number('Radius', 5, min=1, max=20, unit='mm')`, `app.text`, `app.flag`, `app.choice`, `app.file`, `app.save_as`, `app.folder`, with `app.title` for the
  heading, `app.asset('step/part.step')` for a file that comes along and `app.output` for a file it makes -- and **File → Export as app...** bundles it into **one `.fieldesapp` file**, together with the custom blocks it uses (only those), the files it needs and the Python modules next to it.
  In the editor an input is just its default, so the script is written and tried as any script is. See [Apps](docs/apps.md).
- **A double click opens the form.** The app window has the inputs (a number box, a drop-down, a check box, a path with Browse / Save as, a file can be dropped on it), a **Run** button that becomes **Stop**, a progress bar, the log, and the **Outputs** -- the files the script made -- with **Open** and **Show in folder**.
  Every value is checked before the script runs (a number outside min..max, a file that is not there) and the message stands under the input in red. The form remembers what was chosen last time. It is a program of its own, so apps and the editor sit side by side. **File → Preview as app** shows the engineer the same window on the script as it is,
  and reads it again by itself when the script is saved.
- **Watch: it runs again by itself.** With *Run again when a file changes* (or `--watch`) the app runs when a file chosen for an input changes -- save a model from the CAD program and the output is written again -- and when the app file itself is replaced by a new export (the form is read again, what was typed stays).
- **It runs without a window too:** `FielDes run bracket.fieldesapp --Radius 7 --Material aluminium` (inputs are `--name value`, `--help` lists them, `--watch`, `--log FILE`, `--quiet`, exit code 0 / 1 / 2), `FielDes export script.py -o bracket.fieldesapp` and `FielDes info bracket.fieldesapp`; the same through Python as `python -m fieldes run ...`.
- **FielDes takes over the file type.** The Windows installer registers `.fieldesapp` for the user (and removes it with the program); the Linux installer and the `.deb` bring the MIME type. The first time an app is exported FielDes asks whether a double click should open it, and **Settings → Open .fieldesapp files with FielDes** does it for a copy that was not installed.
- **`examples/24_app_bracket_maker.py`** is a complete app: a model file (the bracket comes along inside it), a mode, a wall thickness and where to save the result, with the comments that say how an app is made.

### Every shape is ONE primitive
- **One primitive per shape, with that shape and nothing else.** The library had several of nearly every shape; now there is one: **`box(a, b)`** (a true distance outside it; was `box_exact` -- `box_mitered`, `cube`, `box_centered`,
  `box_exact_centered`, `box_mitered_centered`, `cube_centered` are gone), **`rounded_box`** (`rounded_cube` is gone), **`cylinder(radius, height, base)`** (was `cylinder_z`), **`cone(radius, height, base)`** (was `cone_z`; the
  angle form `cone_ang` is gone), **`torus`** (was `torus_z`), **`pyramid`** (was `pyramid_z`), **`rectangle(a, b)`** (was `rectangle_exact`; `rectangle_centered_exact` is gone), and `gyroid` is not a primitive
  (a gyroid is `cell_periodic('gyroid')`). The 3D shapes stand along z, the 2D shapes lie in z = 0; one that stands another way is turned (`rotate_x`, `rotate_y`, `rotate_z`), one that sits elsewhere is moved (`move`).
  **The old names are gone, not aliased** (a beta change): the examples, the docs, the tour, the menus (*New 3D shape*, *New 2D shape*) and the library say the new ones. A script from before needs the names changed.

### Every call FielDes writes is laid out one argument to a line
- **A call with two arguments or more is written one argument to a line, whatever its length** -- from the right-click menus and from the completion alike: `sphere(5, (0, 0, 0))` is three lines, `polyline([a, b, c], closed=False)`
  is three. A call or a list inside it stays on one line when it fits (88 columns). The exception is `handles(...)`, whose numbers nobody edits by hand: it is one long line. (`point(...)` and `var(...)` are notation, and stay as they are.)

### Shapes made of points: profiles, lines and extrusions
- **A 2D shape from any number of points: `profile([a, b, c, d])`.** The face a closed outline through the points encloses, in **the plane through the first three points** -- every other point is projected onto that plane, so it works
  whatever the points are. Each point is a **corner** (a hard edge) or a **spline point** (the outline goes smoothly through it): `profile(points, smooth=False)` says which a point is that does not say itself (a hard edge by default),
  `point(x, y, z, smooth=True)` says it for one point. A profile is an exact distance field, concave outlines too, so it offsets, shells and extrudes like any 2D shape.
- **Two 1D shapes, a set of lines and a smooth curve: `polyline([a, b, c])` and `spline([a, b, c])`.** A new kind of model, open or `closed=True`, drawn as a line, with its own icon, and **two entries under New 1D shape** (not one
  entry with a switch inside): the point of each can still say otherwise (`point(..., smooth=...)`). A line stands on its own: it is the path of `path_extrude`, the outline `profile(line)` fills,
  and, as the distance to the line, `offset(line, 1.5)` is a tube round it.
- **A point of a profile is moved by its dot, in the plane of the profile.** Press the dot (selected or not) and drag; it has no move arrows and no plane squares -- only the dot, **one rotation ring** in the plane (on a spline point), and the one scale handle.
- **The rotation of a point turns the direction a spline leaves it -- one angle, in the plane of the profile, for a point of a profile.** A point has had a gizmo with rotation rings and no use for them; now the rotate numbers of a spline point turn its tangent
  (no rotation: Catmull-Rom, from the neighbours). **A point of a profile has one rotation ring, in the plane of the profile**; a point of a line in space has the three. A line through the selected point shows the direction.
- **The scale of a point is how strongly a spline leaves it and how thick a sweep is there.** A point has one scale (its one scale handle moves the three numbers of its gizmo together; so does Shift + wheel): `1` is the Catmull-Rom curve, more is a rounder bulge, less
  is tighter. In `path_extrude` it is the thickness of the profile at that point, tapering smoothly between points: a path with a point of scale 2 makes a tube twice as thick there.
- **`path_extrude(profile, line)` sweeps a 2D shape along a line**, turning with it, with mitered joints; the same rotation of a point **twists** the profile about the path there. **`extrude(shape, a, b)`** is the straight one,
  and follows the plane of a profile (it replaces `extrude_z`, which is gone: for a shape in z = 0 it is the same call).
- **Edit them in the viewport, as if the shape were a rubber band.** The edges of every 2D shape are drawn **blue** (and the lines of curves). **Press an edge and drag: a point is pulled out of it**, drawn at once as it follows the mouse,
  and written into the script when you let go; on a line **press the line and drag**. **Drag the face of a profile** and all its points move together, in its plane. **Double-click a point** to change it between a corner (a square dot) and a spline point
  (a round dot with a ring) -- written as `smooth=True` / `smooth=False` on the point; **a dot always says what the point is in its profile or line**, however the profile was made. **Double-click a profile** and the view turns to face its plane. A hint beside the mouse says what a click does.
- **A profile from a surface: `profile(surface)`.** Select a surface picked on a part (`select_surface`) and make *New 2D shape → profile*: a 2D shape of the surface's outline, holes included, in the plane of the face. **A flat face is exact** (its edges are found against
  the part's own field, its corners are where the edges meet); **a surface that is not flat is projected** onto the plane through its middle, along the way it faces on average. It moves with the surface, and extrudes, offsets and sweeps like any 2D shape.
- **A profile made of points that are not in one plane puts them there.** The plane through the first three points that are not on a line stays, and every other point is **moved onto it**: its numbers in the script change, in the same undo step that makes the profile.
- **The menus follow the library's own naming: shapes are nouns, operations are verbs.** *New 2D shape → profile* with three or more points selected is made from them, in the order selected; with none selected it makes four points and a profile. The same for
  *New 1D shape → polyline* and *spline* (two or more points). `extrude` and `path_extrude` are operations: *Operation → Extruding*, offered for a selected 2D shape (and a line, or the points of a path). See [Profiles](docs/profiles.md).
- **The selection rectangle selects every kind of model.** Points, sets of points and fields could not be selected by dragging a rectangle (it only took shapes with a mesh); now they are, and so are lines.

### Live extrusions, symmetry in the work plane, and points scaled and turned together
- **A straight `extrude` of a profile follows the points as you drag them.** The solid is a prism -- the outline pulled along its normal -- so the viewport makes it from the numbers of the points (and of `a` and `b`) at
  every frame, the way it makes the face of the profile: no script is run and no field is meshed, while you drag and after you let go. A spline-shaped (`smooth=True`) profile pulled out of a 2 mm region went from a render of
  3.4 s after every release to 0.15 s. It stays a prism while its numbers are what it was made with (a gizmo has not placed it, no face of it was pulled); otherwise its field is meshed as ever.
- **A sweep (`path_extrude`) of a profile along a line of points is drawn from the numbers of its points too.** The viewport carries the outline along the path -- a ring at every piece, in the frame the field of the sweep has (turned by the rotation
  of the points, scaled by their scale, mitred at the corners, capped with the face of the profile) -- so no field is meshed, and dragging a point of the profile, of the path or its gizmo follows at once. A circle or a rectangle as the
  profile, and a sweep with a colour field, a channel or a result, still come from the field. `FIELDES_NO_NATIVE=1` draws every prism and sweep from its field, to compare.
- **A sweep no longer freezes the program: its field is not made unless something reads it.** The field of a sweep is a piece for every piece of the path, each holding the whole outline of the profile, and making it was most of the time:
  optimising the tree and building the eight evaluators (together 6 s for a smooth profile of 15 points swept along three points), keying it among the shapes of the run (0.3 s), and -- in the GUI's thread, so that the window did not answer --
  a search through the whole field for the shape's extent (6 s, then 9 s again: 15 s of a frozen window, for a mesh that is made in 1 ms). A shape that is drawn from the numbers of its points (a sweep, a straight extrusion) now makes none of
  that: the script runs in 0.5 s and the sweep is on the screen 0.09 s after (before: 5.9 s, then 14.6 s). Its field is made, once, by the first thing that reads it (a boolean, an offset, an analysis, a field viewer, a sweep whose numbers
  can no longer be drawn directly), in the thread that needs it. The search for a shape's extent -- which had a budget of 4 ms but read the clock only every 256 evaluations, each one tens of milliseconds on such a field -- reads it at every
  evaluation now (with 50 ms, which the shapes of the examples need a tenth of), and is not made at all for a tree of more than 100 000 nodes (what is made of a sweep: a difference, an offset), so no big shape can hold the window any more; it is rendered at the scene's resolution. The render cache's key -- a hash of the whole field, about 9 s for a four-point sweep -- is no longer asked of a mesh that is made from numbers either.
- **The first ring of a sweep is the profile itself, at any angle, and the path runs through the middle of the cross-section at all times.** A sweep used to turn the profile across the path at its start, so a path that left the profile at an angle gave a cap that was not
  the profile. The profile is now carried along as it is -- its own plane at the start, a slanted sweep when the path is oblique -- exactly the same from the field and from the viewport. And the path is **drawn where the sweep goes**: from the middle of the profile,
  through the middle of the cross-section, its points with it, whatever numbers its points have in the script (its shape is what matters; before, the sweep started at the profile and the line you drew stayed beside it, wherever you had drawn it). A point of it is
  dragged where it is seen; the first point stays at the middle of the profile.
- **A point of a curve has a gizmo that faces the way the curve does.** Its blue arrow and ring are along the curve at the point, the red and green across it: the blue ring twists what is swept, the other two tilt the direction the curve leaves the point by.
  The numbers written are the same angles and move numbers as before.
- **The scale of a point no longer "goes back" when you let go (Shift + wheel above all).** Every change a drag or the wheel wrote into the script started a run 250 ms later -- one that began between two turns of the wheel read the text of that moment and, when it was
  done, put its older numbers back over the ones being dragged, so some turns were lost (eight notches scaled by five). No run begins while a drag or the wheel is going, a run that was on its way when it began is not shown, and one run follows the gesture.
- **Pressing on the tube beside a point no longer freezes the program.** A press on a sweep took it for a surface to pull and made and compiled its whole field in the GUI's thread -- seconds for a small one, minutes for a profile of many points. A sweep has no
  surface to pull (its numbers are its points'), so the press selects or draws a rectangle as it does on empty space.
- **The handles of a rectangle, a rounded rectangle and a triangle.** A `rectangle` is made of its four sides (`handles` lists `[x0, y0, x1, y1]`) instead of a centre and half sizes that tied the opposite sides together, so every side can be pulled and moves
  alone; a `rounded_rectangle` is the rectangle drawn in by the radius and grown by it again, so its corners stay tangent to the sides at any numbers (five handles: the sides and the radius; it is now an exact distance too); a `triangle` has three handles, its
  sides, each pulled parallel to itself. The gizmo of each stays at the middle of the shape when a side is pulled -- its pivot is an expression of the handle numbers -- and nothing jumps when the script runs again.
- **Symmetry in the work plane.** The card has a *Symmetry* row (Off, 1, 2, 3, 4): axes through one centre, equally spaced (one axis; two at right angles; three 60 degrees apart; four 45 degrees apart), drawn as dashed
  magenta lines. Whatever you put down is put down at its images too (a point gets 2, 4, 6 or 8 points; a line, a spline or a shape gets a shape for each image, previewed while you draw and written as `profile_1_m1` ...). The dot where the axes
  cross is dragged to move them (onto the grid or a point), the knob on the first axis to turn them (steps of 5 degrees while snapping, the angle shown).
- **Several selected points can be scaled and turned evenly.** The shared gizmo of a group of points has two rings round the middle of them: the gold one scales them about it (their proportions kept), the one in the plane turns them
  about it; the factor and the angle are shown. What changes is the move numbers of each point, so every profile and line made of them follows.
- **The path of a sweep goes through the middle of a profile profile, and the sweep starts at the profile.** `path_extrude(profile, curve)` carried the *first point* of the profile along the path, so the profile sat beside it. It is now the middle of the box of the
  outline -- of a circle or a rectangle as of a profile --, and the thickness of a path point scales the profile about that middle. And the 2D shape is the base of the sweep: the whole sweep is moved so that the first point of the path is the
  middle of the profile, and its first ring is the profile itself (see below for a path that leaves it at an angle), which is what you see while you edit the profile. The path is
  the way the sweep travels, not where it begins.
- **A point can be pulled out of the path of a sweep again.** The path now runs inside its own sweep, and the viewport took the sweep for being in front of the line, so pressing the spline did nothing. A curve's line is now pressed through the body it runs in: drag it and a new point is put on it (written in the script's own numbers, not the shifted ones drawn), as for any other line. A shape that is drawn from the numbers of its points is made in a single pass, and the script of a drag runs the moment you let go, not after the pause that waits for typing.
- **A sweep of a primitive -- `path_extrude(circle(3), path)`, a rectangle, a rounded rectangle, a triangle, a polygon -- starts at the 2D shape and is drawn from the numbers of its path, as the sweep of a profile is.** The shape is the base of the sweep (the whole sweep is moved so that the first point of the path is the middle of the shape, and the first ring is the shape in its plane z = 0), and the path runs through the middle of the cross-section, as it does for a profile. The outline (a circle is 72 short sides, a rounded corner 12 to a quarter) is carried along the path in the viewport without meshing the field, also when the shape was made from the right-click menu, with the `handles(...)` and `gizmo(...)` that put round it (moved, turned about z or scaled in its plane, the outline follows): a point of the path is followed at once while it is dragged, and the picture after it is let go is made at once -- where the field of the sweep was meshed again at every step (20 to 90 ms each) and for seconds after (9 s for a circle made from the menu). A shape that is anything else (an `offset` of a circle, a union, a shape a gizmo has turned out of its plane) is meshed from its field as before, and its sweep has a box now, so that the render region grows to hold it (the sweep of a circle made from the menu was cut off at the edge of the region).
- **A point is dragged by its dot, selected or not, with or without a gizmo.** Press the dot of a point and drag: it moves at once -- in the plane of its profile, or in the plane that faces the camera for a point of a line -- where it used to need the point to be selected and its gizmo's dot taken. A click that does not move still selects it; several selected points are moved by the dot of their group.
- **A 2D shape made from the right-click menu is made where you clicked, in any view.** A circle, rectangle, rounded rectangle, ring, polygon or triangle lies in the plane z = 0, but the place a click gave was in the plane through the origin that faces the camera -- which is the plane z = 0 only when you look straight along z, so in any other view the shape was made away from the click. It is now made where the ray under the cursor meets the plane z = 0 (on the work plane while its view is open); a profile, which is made of points in space, is placed as before.
- **A model of the wrong kind is not accepted into a call.** Dropping a 2D shape where a `path_extrude` takes its path used to put it there and fail when the script ran. The tree now refuses it at the drop, with the reason (*The path of path_extrude() takes a curve or a point: 'rounded_rectangle_1' is a 2D shape, so it does not go there*), whether the drop is on a placeholder or on the call. What each place takes is what the library says of the call (the kinds the menus offer an entry by: `path_extrude` a 2D shape, then a curve or points; an operation like `offset` or `lattice` geometry, not a material or a condition); where it says nothing, the run still says if a model does not fit.
- **`extrude` is explained** in [Profiles](docs/profiles.md): what `a` and `b` are, which side is up (the order of the first three points), and that the render region grows to hold the solid.
- **A model's box is known for more shapes**, so that the region follows them without a search: an extrusion of a profile (the outline's box carried along the normal), `move`, and `union` / `intersection` / `difference`
  (a box that holds the result). Before, a profile with `smooth=True` could not be searched for at all, so its extrusion never made the region grow.

### Two kinds of duplication, one scale handle, and a tidier work plane
- **A model moved out of the render region is taken in again, whatever it is.** The region follows the models (Auto scale), but a primitive that was
  moved with its gizmo kept reporting the box it had before the move, so the region did not grow and the model was cut off at its wall. The box a model reports is now
  the box of what it is *at the end of the script* -- after `handles(...)` and `gizmo(...)` have placed it -- for every model: a model that has no box of its own (a
  difference, an offset, a lattice, anything made of other models) gets it searched for once, kept by its expression, so moving it only moves the box. A face that is pulled
  out of a box (or a radius made larger) widens the box by what the numbers have changed, so it is held too. Checked in real windows: a box and a drilled plate dragged far out of
  a small region, a face pulled out of a box -- the region grew and nothing was cut.
- **The region is never made smaller while a model that is meshed in it has no known extent** (it might be what holds that model): a model that the search for its box ran out of
  time on made the region shrink round the others, and cut that one off. The search itself has more time (0.4 s for a first look, 1 s for the second, 1.5 s for a scene, not 0.08 /
  0.5 / 0.6) and is kept by the model's expression, not by every number of the script, so dragging one model no longer makes every other one be searched again.
- **`auto_scale=True` is in the script.** It was the default, and a default was never written, so a script said nothing about the region following the models or about every model being
  meshed at the resolution of its own size. Now every `view.set_bounds(...)` FielDes writes -- a new script, the region growing, *Auto scale* switched back on, an import's `roi(...)`, the
  guided tour and the 28 examples -- has `auto_scale=True` in it (`auto_scale=False` as before turns it off), and the tooltip of the *Auto scale* row says that it also sets each model's resolution.
- **The guided tour covers the new tools**: *Draw on a work plane* (Shift+W; Show me draws a rectangle), *Pull a point out of an edge*, *Corner or spline point* (double-click), *The gizmo of a point*, *Hold Shift* (the plane squares and the one ring),
  *Copy or link* (Shift+D, Ctrl+D) and *Keys worth knowing*; and three new examples show them: **26** profiles and sweeps, **27** copies and linked copies, **28** a profile on a plane and the work plane. Translated in all six languages.
- **Dragging a gizmo no longer meshes the shape again at every mouse move.** What a drag of a gizmo's numbers does is place the same mesh somewhere else, so the mesh that is drawn is drawn again with the move / turn / scale matrix of the numbers
  and nothing is meshed until the drag ends (one real render, at the numbers it ends on). On a heavy lattice, a 40-step drag: 43 meshings became 1, and the frames that came late (mean) from 136 ms to 20 ms -- the display's own refresh. The moved mesh
  differs from the real render in under 0.2 % of the picture. Shapes that really change still mesh as before: those made of what moves (a union that holds the dragged part), coloured by a field, results, and anything cut by the section view.
- **The work plane opens in Line**, so the first click draws.
- **`Shift+D` copies a model, `Ctrl+D` links it.** Shift+D is the duplicate that was Ctrl+D (the statements written again under the model with free names). **Ctrl+D writes `copy = instance(model)`**: a block that **is** the model and follows every change of
  it, and can only be **moved and rotated** (its gizmo has no scale, its surfaces cannot be dragged). The original is held by a **shadow reference** (`# shadow: model`) **always**, locked or not, so it keeps its own row in the model tree. Also
  *Operation → Repeating → instance*. In the editor `Ctrl+D` is still the next occurrence.
- **One scale handle for a point, and for the Shift gizmo of any shape: a ring.** A point scales uniformly only: one gold ring round its dot -- drag it outward to grow, inward to shrink; a faint ring stays at the starting size and the factor is shown --
  instead of three axis squares. The Shift gizmo of any 3D shape has the same ring in place of its three gold squares. (It is the uniform scale handle of Blender's scale gizmo; Maya, Unity and Unreal put a cube in the middle, which here is the dot that moves
  the point.) The normal gizmo keeps its three axis squares.
- **A point of a profile shows only what it can use.** Selected, it has no arrows (its dot moves it in the plane), the **scale handle**, and a **rotation ring only when it is a spline point** (a corner does not leave in a direction). Several points of one profile
  selected together have just the dot.
- **Fixed: a profile drawn from its points went out of date after an edit.** Adding a point (pulling it out of an edge, or the work plane's), or changing a point between a corner and a spline point, kept the old shape on the screen -- the face, the blue outline and the
  dots stayed as they were, and a second profile could vanish. A shape made of points is now told apart by its points (how many, what each says of itself, which numbers make them), so an edit that changes them makes it again. The in-plane move of a
  profile no longer writes a tiny number such as `-7e-08` in the z move.
- **Fixed: the blue outline of a profile was not drawn.** Since profiles are drawn straight from their points they were no longer counted as flat shapes, and only flat shapes and lines had their outline drawn.
- **A point from the right-click menu is exactly where you clicked.** It was moved into the drawn region and rounded to whole millimetres (four clicks gave points up to 600 px from the pointer, two of them in the same place); now it is on the surface
  under the cursor, else in the plane through the origin facing the view (the work plane while it is open), with the numbers of the click.
- **A right-click on a model keeps the *New ...* entries.** The menu of a model had no way to make a shape or a point, so a place to put one could be hard to find when everything under the cursor was a surface; they are in it now, made where you clicked on the surface.
- **`path_extrude` leaves the shape's own points out of the path.** Selecting a profile together with the points along its path put the profile's corners into the path too: a spline round the outline and along the path, dozens of pieces each carrying the whole profile,
  which made the renderer hang. The profile's points are left out, and a path of more than about 160 pieces is thinned (every point that was made stays) so a long sweep can still be drawn.
- **A new box's gizmo is at its centre.** The centre was searched for in the time there is, and a small shape in a large search was not found, which put the gizmo at the origin. The primitives (box, sphere, cylinder, cone, pyramid, rounded box and the 2D shapes)
  know their own extent, so the gizmo, and the region a model is shown in, come from it at once and exactly.
- **The corner of a profile has no scale ring** (as it has no rotation ring): its scale does nothing there.
- **The work plane's modes look like what they make.** There is no *Select* button: **Line**, **Spline** and **Point** are three buttons with a picture of what each makes, and none on is not drawing (click the one that is on, or right-click in the viewport). While drawing the pointer is a
  crosshair and a marker shows the point a click puts down -- a square for a line, a round point with a ring for a spline, a dot for a point; the points already put down have the same marks. Not drawing, there is no marker or readout and the pointer is the usual one.

### A work plane to draw on
- **`Shift+W` opens a work plane view; `Ctrl+Shift+W` makes a new work plane; `W` turns the camera to face it.** A light version of a CAD program's profile plane that writes nothing into the script by itself: the plane is drawn with a grid and has a card
  (grid, snap to grid, snap to points, step, offset, set the view to the plane / the plane to the view). A new plane is the plane of the selected 2D shape, or parallel to the camera view through the origin. The camera turning with
  the middle button leaves the view.
- **Draw in it:** three kinds of drawing, picked in the card or with `E` (line, spline, point), and a **right click to stop drawing** (`E` then brings back the kind of drawing that was on before; nothing is put down, and things are selected, moved, turned and scaled as usual; the view opens in Line, so the first click draws;
  in a drawing mode a click always puts a point down, attaching to a point that is there). Click to put down the points of a line -- **a click on the first point closes it and makes a profile** (`Enter` or a double click ends it open as a
  `polyline`), the same with a spline (`smooth=True` when closed), or a point at a click. Nothing is written until the line ends; then the points and the shape are one undo step. The points are exact: snapped to the grid, and to the points
  that are there.
- **The new things go where the plane is:** right-click → New ... places at the plane, a profile made of new points is in the plane's own axes, the dots of points move in the plane and land on the grid, and pressing a 2D shape in the plane
  moves it about in the plane in steps of the grid (in select mode).
- **The section view is `Shift+X`** (was `Ctrl+Shift+X`): a single key with Shift, like the work plane's, it acts while the viewport -- or the model tree -- has the keyboard, so it never types over the editor.

### Profiles, lines and sweeps are plain math, and fast
- **No black box: the field of a profile, a line and a sweep is an expression of x, y and z.** A profile is the distance to its outline (the least of the distances to its edges) with its side taken from triangles the outline is cut into
  when the script runs -- a point is inside when it is inside one of them -- so the renderer drops the edges and triangles that are far from where it looks, and **a profile of a hundred edges costs about what the few edges near the point cost**.
  An outline that crosses itself uses the winding of the outline round the point instead. A line is the least of the distances to its pieces. Measured on the same 140 x 120 x 80 mm region: a six-point plate extruded in **0.06 s**, the
  same plate swept along a 43-piece path in **1.4 s** (1 sample per mm) and **3.3 s** (2 per mm); a sixteen-point *smooth* outline of 224 edges extruded in **1.9 s** and **8.6 s**.
- **Dragging a point is live.** The face of a profile and the line of a curve are drawn from the numbers of the points straight away, with no run of the script and no field: a six-point shape no longer lags, and the render bar moves while a sweep is made.
- **Adding a point to an edge feels like pulling it out of the edge**: the new point is drawn at once and follows the mouse; the script is written when the mouse is let go.

### Hold Shift: a gizmo of planes, and the wheel scales
- **Holding Shift changes the gizmo.** The three move arrows become three **flat squares**, each moving the model along a plane; the three scale squares turn gold, and any of them scales all three axes together. It works for one model and for the shared
  gizmo of several.
- **Shift + the mouse wheel scales what is selected** -- every selected model by its gizmo's scale numbers, a point by its scale -- a notch is 10 %, written to the script when the wheel rests (one undo step).

### A 2D shape is a face, not a thin body
- **A 2D shape is drawn as what it is: a flat face in the plane z = 0, with no thickness.** It used to be cut out of a thin slab (a tenth of a millimetre or so), which showed edges and a back when you looked at it from the side. The face is made from the shape's own field (its outline is found where the field is
  zero, the inside is filled with flat triangles), so a hole is a hole and an offset is an offset; a shape that is coloured by a field has the face cut small enough for the colour. It is a sheet in the same sense as a surface, with no curvature.
- **A surface is drawn as the sheet itself, too.** It was a layer a voxel thick (the field's absolute value less a little); it is now the zero set of the field, meshed as it is: one layer of triangles.
- **Pulling a 2D shape pulls its edge.** There is no surface inside a face to take hold of, so a press on a flat shape grabs the part of its outline that is nearest to the cursor (when that is within a few pixels of it), and the numbers behind that edge change -- a rectangle's side, a circle's radius, a hole's wall.
  Away from the outline a press selects the shape, as it does on any shape that has nothing to pull there.

### Dragging a surface moves that surface and nothing else
- **A face you drag is the only thing that changes.** Dragging a cylinder's wall (or a hole's, or a sphere's) changes its **radius** and leaves its **centre** where it is; dragging one face of a box moves that face and the opposite one stays; dragging the top of a cylinder leaves its bottom. The same
  for the faces of an imported part: its hole walls keep their axes. (The centre of a round feature has a heavy weight in the drag's solver; it only moves when nothing else can take the drag.)
- **The gizmo's pivot no longer moves while you drag faces.** A part that the gizmo rotates or scales turns about the middle of its box -- and dragging a face changed the box, so the part jumped sideways when the script ran again. The pivot is now the middle of the box the shape had before its faces were dragged.
- **A press next to the gizmo is a drag of the surface.** Over a surface that can be dragged, the gizmo's arrows, rings and squares only take a press where they are drawn (a drag of a face beside a scale square used to scale the part). Away from the part the gizmo keeps its generous reach.
- **`handles(...)` is one line.** The numbers of a shape's surfaces are written on a single line, however many there are (the exception to the one-argument-to-a-line layout of every other call): nobody edits them by hand, they are dragged, and the call stays out of the way in the editor.
- **One fold arrow per model.** The line that defines a variable folds together with its `handles(...)`, `gizmo(...)` and `lock(...)` statements, instead of an arrow on each of them; the model tree's row closes with it.

### The viewport is Z up by default
- **Z points up** in the viewport unless you choose otherwise, as the shapes of the library are made: a cylinder stands along z, an extrusion goes up along z, a 2D shape lies in the z = 0 plane.
  **Settings → Rotation mode → Turntable (Y up)** is still there, and what you choose is kept.

### An app can have a viewport
- **`app.viewport(models, ...)`** gives the app window a picture of what the creator names -- *opt-in*, an app without it is the form it was. `app.viewport({'Beam': body, 'Load point': pull}, support, load)`: each name is drawn over its model in a card
  in the style of the labels of boundary conditions (`labels=False` turns them off), the loose arguments are drawn without a name (the supports and loads show their symbols). The person looks around with the mouse as in the editor, **clicks a model to select it and
  drags it by its gizmo and by its surfaces** -- the models the creator did not `lock()`. There is no model tree and no context menu in it. What is dragged is written into the numbers (`var()`s) of the app's script, in memory (not into the app file), and the picture
  is made again; **Reset the picture** takes it back.
- **The picture is made on its own -- when the app opens, when an input changes, after a drag -- and the work waits for Run.** `app.running()` is False while the picture is made and True when the person presses Run (and in the command line); the analysis and the
  files go under `if app.running():`. See `examples/25_app_beam_load_case.py` (a locked beam, a load point to drag, the stresses on Run) and [Apps](docs/apps.md).
- **In the editor the viewport is only a setting**: it draws nothing and has no row (no block) in the model tree; the models it names are in the tree as
  they are, shown by their eyes, and *File → Preview as app* shows the viewport. In the app only what the viewport names is shown (a model that the script
  also displays by its name is not drawn twice).
- **The result card of an app (and of the editor) lets go of the mouse.** A click on the key numbers of the card (its rich text took the move and the
  release of the click) left the card stuck to the cursor, following it about and not to be dragged or sized from there. The card's labels now let
  the mouse go on to the card, and a card whose button is no longer down lets go at the next move.
- **The app window has the FielDes look**: the dark teal of the viewport, cream text, the same fields, buttons and check boxes, the form on the left and the viewport on the right.

### Points are always there, as the middle point of their gizmo
- **A point is a dot in the viewport, always** -- the middle point of the gizmo it can have. Click the dot to select the point (its full gizmo shows then; drag it to move the point). A point has an **eye** in the
  model tree again: hidden, its dot is gone; `lock()` keeps the dot and takes the gizmo away, and `mode='never'` leaves only the dot. Points made at positions given by `var()`s or moved by `gizmo(point, move=...)` stay where they are made (`move()` takes such a point too).
  This is what makes points useful in an app's viewport: the person drags a load point, a support, a cut.

### The field viewer: one viewer for every field, with an icon you place
- **Every field has an icon in the viewport, with its name over it** (the style of the labels of boundary conditions), so with several fields you see at a glance which is which. The icon is where the object the field was made from is -- a point, the middle of a body
  -- and a field that is made from nothing starts at the origin. **Click the icon** (or the field's row in the model tree) and the field's viewer opens; **drag the icon** and the plane of the viewer goes with it.
- **Each field has its own viewer: the disc, as it was.** A coloured **disc** (X, Y or Z) with a **Radius** slider and box, the opacity, the legend, the value under the mouse and the flat 2D view; nothing limits it to a box -- the arrows take it wherever the field is worth looking at. In the viewport the disc has the section card's **arrow along the way it faces**, **two arrows in its plane** and the **dot in the middle** (moves it freely in its plane). Several fields can be open at once, a drop-down on the card chooses the one it is about; each has its own icon, eye and disc.
- **The icon never hides a point.** A field made from a point starts on it; its icon is drawn beside the point's dot (several icons that would overlap are moved
  apart too), so a click on the dot selects the point and shows its gizmo. Drag the icon and the disc goes with it.
- **The eye of a field shows or hides its icon and its viewer** (it writes the same `# hidden:` line as for a body; a field the script displays is shown). A field made from the menu is displayed at once. A field has no model gizmo: its icon and its disc (with its arrows and dot while the viewer is open) are what is moved. In an app's
  viewport a field of `app.viewport(...)` is an icon too, so the person can slice a distance, a stress or a temperature where they look.

### Handles and the gizmo have their own names: `handles()` and `gizmo()`
- **`expose()` is now `handles()`, and the old `handles()` is now `gizmo()`.** The names were the wrong way round: the *handles* you drag on a shape's surfaces were made by `expose()`, and the arrows, rings and squares of the *gizmo* by `handles()`. Now the word means the same thing in the interface, the documentation and the script:
  `x = gizmo(x, move=(var(0), ...), rotate=(...), scale=(...), mode='click')` writes the gizmo; `x = handles(x, [var(...), ...])` makes the surfaces of a shape draggable. `lock()` is unchanged.
  **The old forms are gone** (a beta change, no compatibility shims): `expose(...)` no longer exists, and `handles(x, move=..., rotate=..., scale=..., mode=...)` is refused with a message that says it is `gizmo(...)` now. Files from before need
  the two names swapped, and the examples (04, 18, 20) are converted. The model tree, the keys (`E` for the gizmo mode, `R` for the lock), the tour and the translations say *gizmo* and *handles* in the same way.
- **Both calls are written one argument per line, like the arguments of an operation**, so each can be found and changed by hand:
  ```python
  sphere_1 = gizmo(
      sphere_1,
      move=(var(0), var(0), var(0)),
      rotate=(var(0), var(0), var(0)),
      scale=(var(1), var(1), var(1)),
  )
  sphere_1 = handles(
      sphere_1,
      [
          var(0.0),
          var(0.0),
          var(0.0),
          var(1.0),
      ],
  )
  ```
  The list of numbers of `handles()` is one number to a line up to 12 numbers, eight to a line beyond that. Changing the gizmo's mode (the button, the key `E`) writes `mode=` into that call as one more line.

### Auto scale: the render region follows the models, and every model is meshed at its own resolution
- **`view.set_bounds(lo, hi, auto_scale=True)`** -- on by default. The numbers of the region stay in the script, and with Auto scale on FielDes **rewrites them live**: the region grows when a model reaches out of it (you drag it, type a bigger number, add a model -- you see the call change as you type), and
  **shrinks** when the models have become much smaller or the big one is gone. A file that is just opened is only grown; a scene with no model left does not touch the region (no region of zero size). With `auto_scale=False` the region is exactly what is written. **Render settings has an *Auto scale* row** that switches it.
- **Every model is meshed at a resolution of its own, scaled from `view.set_resolution`.** The number is for a model as big as the region; a smaller model is meshed finer in proportion to the square root of how much smaller it is (at most 2000 samples across it), so a large and a small part render at the same time without the
  small one being a jumble of triangles, and raising `view.set_resolution` raises every model. **The quick first picture (the "LOD") is sized by the model, not by the region**: a really small model used to get a handful of triangles first.
  Models with a resolution of their own (an imported part's, a render hint) keep it.

## 0.6.1 — the flow analysis takes a body, the guided tour works again, points are only their gizmo, one look for the planes

### Cells and lattices are their own kind, with a section of their own in the menus, and random points for foams
- **A *Lattice* section in the right-click menus** (*Add lattice* on empty space): the operations that fill a model with a lattice (`lattice`, `lattice_surface_conform`) -- they are no longer among the operations -- the foam in three steps (`random_points`, `voronoi_lattice`, `delaunay_lattice`),
  and under them **every cell the library has**, a menu for each family (*Strut cells*, *TPMS cells*, *Planar cells*, *Custom cells*): *random*, *custom* and every lattice type can now be chosen from the menu. A cell entry writes
  `cell_gyroid_1 = cell_periodic('gyroid')`; a cell selected with a body is the cell of the `lattice` that is written, a field selected with them its thickness.
- **Kinds of their own**: a **lattice** (what `lattice()` and `lattice_surface_conform()` make, with an icon of struts), a **lattice cell** and a **point set** (scattered dots). A lattice is a body like any other once made (the kind is not kept by arithmetic on it).
- **A foam is made around points, in three steps that each do what their name says.** `random_points(body, spacing, randomness, min_distance, max_distance, seed)` makes points in a body with the say over how random
  they are: the average distance between neighbours (calibrated, whatever the randomness), a regular grid (0) to as random as the distances allow (1), the nearest two points may be, the farthest a point may be from its neighbour -- and **FielDes draws the
  points in the viewport, each as the dot of a gizmo** (without its handles: they are data, not something to drag). `voronoi_lattice(points)` makes the foam around them, `delaunay_lattice(points)` a stochastic truss. The points know the body they were
  made in and the lattice fills that body, so there is no second body to contradict them (a list of your own points says it with `body=`). **`cell_non_periodic` is gone**: a foam is not a cell -- a cell is one cell that repeats -- so `lattice()` and
  `lattice_surface_conform()` take cells that repeat, and `lattice(part, cell_non_periodic('voronoi'), cell_size=8)` is now `voronoi_lattice(random_points(part, spacing=8))`; a random graph on a surface is `surface_graph(body, 5, 'voronoi').thicken(0.5)`.
  The menu groups are named like the rest of the program (*Custom cells*, entries `custom`, `custom_truss`, `custom_tpms`; the foam has no group of cells any more). See [Lattices](docs/lattices.md#foams-random_points-voronoi_lattice-delaunay_lattice).

- **`examples/23_lattice_gallery.py`**: one lattice of every kind, each in a simple body of its own (no part needed): octet and kelvin struts, a custom strut cell, a gyroid sheet, a Schwarz P network, a honeycomb, a Voronoi foam (with its points drawn), a Delaunay truss, a foam by relative density, a wall graded by a field, a cell map round a cylinder and a conformal layer on a sphere.

### The guided tour, rebuilt; the property tab; the editor
- **The tour's order makes sense**: selecting a model and its gizmo come right after the tree, before arguments, placeholders and sections. **Arrows point at the row they mean**: the row is framed, the arrow ends at its edge facing the card and never crosses the tree, and the card
  stands where it covers neither that row nor the lines of the script the step is about (it used to cover them, and the arrow crossed the whole tree). The cards are as tall as their words need.
- **Only what a step is about is shown.** The tree closes every row that has rows under it except the ones the step needs, and the steps that need more than the plate -- a field, a reference, a result -- work on **a small model of their own** (a plate, a point, a field, the plate grown by it; a knob and a cap; a plain beam with a support and a load) and
  give the user's model back when they are left. The **Properties** step uses a real, small analysis that renders. The gizmo line that selecting writes does not stay behind.
- **Shadow references are explained**: what it is for first (a model needed in two places; a normal drag would move it out of the first), then what to do.
- **The property tab** comes out of the right edge of the tree, outside it, level with the hovered row (it used to sit inside the row, over the buttons), and the drawer is restyled: the model and what it is, fields and numbers in groups with their glyphs and values, the hint and the export button below.
  **A property is not made of its result**: `von_mises_1 = result.von_mises` has no child row for `result` and no shadow of it. **Every field a solver calculates is a field**, not a body (`result.von_mises`, the temperature, the speed, the density ... used to be of the kind *3D shape*).
- **The fold arrow of the code editor is bigger** (it was too small to see and hit). **The documentation under the mouse -- the signature of a function, the explanation of an argument name -- appears as soon as the mouse rests on the name** (it waited for the long delay of a tooltip), and an answer is kept.
- **The arrow in front of a row reacts to a quick second click** (it was a double click, which Qt ignores on the arrow).

### Examples that run as they are, and installers
- **Every example that starts from a part now uses the bracket that comes with FielDes** (02, 03, 04, 06, 07, 09, 11, 12 and 13 started from sample parts that are not distributed): the analyses, lattices and exports place their regions for the bracket. `03_kitchen_assembly.py` is
  `03_exclude_and_assembly.py` (an assembly of three imported parts, and `exclude()` with a region of your own); 13 tessellates the bracket.
- **The Windows installer starts FielDes only when Finish is clicked** (it started it while its own window still said it was working); the box *Start FielDes when the installation is finished* can be changed until then.
- **Linux**: `scripts/build-linux.sh --package` also makes a **self-extracting installer** (`FielDes-<version>-linux-x64-installer.run`: for the user into `~/.local/share/FielDes` with a menu entry, the command `fieldes` and an `uninstall.sh`; `--system` for everyone;
  `--prefix DIR`; `--uninstall`) and a **Debian / Ubuntu package** (`.deb`: `sudo apt install ./FielDes_<version>_amd64.deb`).
  The release workflow builds and uploads them.

### Files: export to every common format, finite-element models and results, key numbers on the screen
- **`export_mesh(part, "part.stl")`** writes a body as **STL (binary or ASCII), OBJ, PLY, 3MF, glTF (`.glb`, `.gltf`), FBX, OFF and DXF**, in the format of the extension; the importer reads
  them all back (`import_model`): **FBX** (binary and ASCII), **OFF** and **DXF** (`3DFACE` and polyface meshes) are new to it. FBX is an Autodesk format with no published specification: it is
  read and written **without the Autodesk SDK**, from the structure the community documents, and for **meshes only** (see [Meshes](docs/meshes.md#exporting-and-importing-meshes) for what that means).
- **`export_fea(result, "bracket.inp")`** hands a static analysis to another solver -- **Abaqus / CalculiX `.inp`, Nastran `.bdf`, Gmsh `.msh`**: the tetrahedral mesh FielDes solved on, the material, the supports
  (as node sets with their held directions) and the loads (a force spread over the nodes of its surface by the area each carries), gravity.
  **`export_results(result, "bracket.vtu")`** writes what was found for ParaView and others -- **VTK `.vtu`** (the mesh, the displacement and every field at the nodes and in each element), **CSV** tables of the nodes
  and the elements, and **`.txt`** with the key numbers (any kind of result). **`export_field(field, "stress.vti")`** samples any field on a grid (VTK image data or CSV).
- **Export in the menus.** *File → Export mesh…* writes the viewport's meshes in any of the formats (it was STL only). Right-click a model, in the viewport or on its row → **Export**: *Mesh* in every format,
  *Field on a grid*, and for an analysis *Finite-element model* and *Results*; the file is asked for and written in the background by the same functions a script calls.
- **Key numbers on the result card.** The card prints what the result found that matters -- maximum von Mises stress, maximum displacement, safety factor and reactions of a static analysis; **drag and lift
  force and coefficients**, pressure drop and flows of a flow; temperatures; natural frequencies -- under its field drop-down. `result.drag_force`, `.lift_force`, `.drag_coefficient` and `.lift_coefficient`
  are new on a flow result (the direction is the inlets' `velocity=`, or from the inlets' middle to the outlets'; the coefficients use the dynamic pressure of the mean inlet speed and the area of the body's
  bounding box seen along the flow). `result_summary(result)` lists them; the property drawer's last button writes them as text.
- **Bug fixed on the way: importing a mesh failed** (`NameError: _NATIVE` in `mesh_import.py`: the table of the formats the library reads itself was missing, so STL, OBJ, PLY, 3MF and glTF imports raised an error instead of reading the file).

### The Linux package opens with nothing installed
- **The Linux package is self-contained.** `scripts/build-linux.sh --package` now puts **Qt and its plugins, a Python runtime with its standard library, the kernel's libraries and every other library the program loads** into the folder
  (`lib/`, `plugins/`, `runtime/python3/`, found through `qt.conf`, the programs' run paths and `PYTHONHOME`, which the program sets itself when `runtime/python3` is next to it): unpack it and double-click `FielDes`; only the C library and the
  graphics driver of the machine are used. (A copy that was handed to someone had to have Qt installed first.) The one thing a package cannot carry is the C library, so it asks for one as new as the one it was built with: the
  script says which, and **`.github/workflows/release.yml` builds it on Ubuntu 22.04**, the oldest common one, and starts the result in a bare Ubuntu with only a virtual display before handing it over.

### The model tree: arguments, sections, remembered closing, the property drawer
- **A comment or a blank line written or deleted anywhere does not stop a calculation that is running** (adding a section above the model that is being solved, typing a note above it). An edit that left the statements as they were but moved them down
  or up used to count as a change above the running statement and stopped it; the same statements a few lines further on are now put into the run, with the places they keep (the line it is on, the places of its numbers) moved along. A script
  with breakpoints, or a running statement that is written with `var(...)` numbers it has not asked for yet, is still run again for such an edit.
- **Every argument of a call is a row under it** (`radius = 4`, `center = (3, 0, 0)`), in the order of the call, so the tree reads like the code; **a click selects the value in the code editor**, ready to be replaced, as a placeholder is. A statement that has only
  arguments for children starts closed.
- **Sections are rows.** A `# SECTION name` comment is a row with the models below it (up to the next section) under it. **Hover the gap between two rows: it lights up; click it and a section starts there, its name already open for typing**;
  double-click a section's name to rename it (the comment follows every key); its bin deletes the heading only. Nesting stops at a section's border (a model used in a later section is a shadow there), and the heading is no longer
  moved along with the first model below it when that is dragged.
- **What is closed is remembered for each file, and it is the editor's folding.** Closing a statement's row folds its lines in the code editor, closing a section folds the section, and folding in the editor's gutter closes the row -- both
  ways, and both kept when the file is opened again. **A row that holds a placeholder is never closed**, whatever was saved: that is where you have to write.
- **Property drawer.** A hovered result row has an arrow at its end; it opens a drawer with the result's **fields and numbers**; a click writes `von_mises_1 = result.von_mises` under the statement as a model of its own. The result stays where it is
  (a statement that only takes a property of a model holds a reference to it, not its ownership).
- **The guided tour** has steps for all of it (arguments, sections, closing, properties) and a **placeholder step you do yourself**: right-click `drilled`, *Operation ▸ Offsets and walls ▸ offset* makes `offset_1 = offset(drilled, o=...)`, and you type the number.
- **Fixed:** the field of an imported part showed boxy outside its surface in the section card's *Field* mode (it sampled the body's raw tree; it now uses the exact distance), `field_from_body` of an imported part is exact too, and an imported model
  in the static-analysis example can be shown again (the tree refused a tuple-unpacking definition such as `bracket, (lo, hi) = parts[0]`).

### A locked model is read-only; every result is a field; any model can be locked; the viewport follows a running simulation
- **Nothing moves a locked model, and a menu never inserts one.** A surface picked on a locked model (right-click → *Select Surface*) is written below its `lock(...)` line with a reference to it
  (`# shadow: part`) instead of the model being taken into the selection, as for a drag and for the other menu operations. And a plain move, a drop or an operation that would have to move a locked model (to make room for what is made of it)
  is refused, with the reason, in one place for every kind of edit -- so no operation, present or added later, can move it.
- **Every result is a field.** The result of an optimisation is its final design, of `static_analysis` the von Mises stress, of `modal_analysis` the displacement of the first mode, of `thermal_analysis` the temperature, of `fluid_analysis` the speed:
  it goes wherever a shape goes (`offset(result, 1)`, `difference(part, result)`, `result * 2`, the part or the domain of the next analysis). **`result.fields`** lists every field the result calculated as `{name: field}`
  (the attributes by those names stay: `result.pressure`). Stated on its own, a result is shown as before -- an optimisation with all its iterations -- and passing it on does not change that.
- **What you set on the result card stays while a design changes.** The opacity of a flow, the field shown, the deformation, Elements and Flow used to start again from their defaults with every new
  picture of a running optimisation (a new one after every iteration), so what you had lowered or switched was lost at once. The window now keeps them and gives them to every new result shape -- the live pictures and the result of the next run.
- **After an optimisation, playing the steps back no longer meshes each one again.** The steps that were not shown are meshed in the background once the shown one is done, and kept, so the step slider and play (topology and flow optimisation alike) find them all made.
- **Any model can be locked**: the lock button is on every row of the model tree, results and simulations included (`result = lock(result)`); `lock()` takes whatever the script made.
- **The viewport follows a running simulation or optimisation.** While one ran, nothing was meshed on the screen -- a part hidden or shown, a number edited, appeared only when the run was over -- and an edit could not even restart the run until the solver
  had done its meshing (20 s and more on a fine element size). Three causes, each fixed at its root:
  - *the viewport waited for the script's thread.* The notice that a shape's level was meshed was delivered by the thread that runs the script, which handles none until the script is over; it now goes to the window's own thread, so every shape is meshed
    and drawn at once while the script runs. A statement that is still going after 0.4 s -- whatever it is: an analysis, an optimisation, an import -- has what the statements before it show drawn, and the run's own result replaces it when it is done.
    A picture of a run that is not over **never removes** a shape that the last run made from a statement that has not run yet in this one (it is not gone, it is not made yet), so a long render of such a shape is not stopped by it.
    A script whose statements are all quick is drawn as before.
  - *an edit waited for the solver's meshing.* The tetrahedral mesher did not look at the stop flag; it does now, between its phases and in its long loops, so a new edit stops the old run within a moment and the new one starts.
  - *a long render took the processor from the small ones.* A render that is expected to be long (its next level has more than about 150 000 vertices) runs at a lower priority and is **never stopped or made to wait**: it uses every core that nothing else wants. A small shape that is changed
    meanwhile -- and a part that is hidden, which needs no render at all -- goes first (a tiny sphere next to a gyroid that took half a minute to mesh was ready in 0.35 s instead of 4 s). The mesher's threads are now threads of their own instead of tasks of the
    system's shared pool (which makes a second render wait for threads while the first one has them all), and an idle mesher thread gives its turn away instead of looping at full speed.

### An edit that has nothing to do with what is running leaves it running
- **A calculation that is going is not stopped, and not made again, by an edit that does not touch it.** A model shown or hidden above it (a line that only shows a model) and anything written after it are put into the run as it goes: the calculation of minutes (an optimisation,
  an analysis) carries on, no run follows, and what the edit shows -- an unhidden model -- is drawn at once; what comes after the statement that runs is the new text's when the run gets to it. Only an edit that changes what the statement is made of -- a
  number in it or in a statement it uses, a line added above it -- stops it, as before. (The check read the first changed *line*, so a click on the eye of a model above the optimisation stopped it, and the stopped solver's work was lost.) Typing in a text that is not a script yet,
  below the statement, lets the statement end and keeps its result for the run that follows.
- **A long render is not begun again by an edit that has nothing to do with it.** The design an optimisation made, smoothed (`smooth(result, ...)`), is a field made of copies of the result's own field moved by a few steps; such a moved copy had no key of what it is, so every
  run of the script made "new" shapes of it, and the smoothed design -- a render of one or two minutes -- started over at every edit, even of a cylinder next to it. **Every oracle now has one**, with no class saying it for itself: what an oracle says it holds
  (its persistent key) and the keys of what it depends on make its content key, in the base class. An oracle that says nothing of what it holds is still told apart by itself, the safe answer.

### The flow result card has an Opacity slider; a model dragged out of a call that needs it leaves a placeholder
- **A flow can be drawn translucent**, so that the body in it shows: the result card of `fluid_analysis` and of `flow_topology_optimization` has an **Opacity** slider (as the field viewer's), and the body stays solid inside the fluid. The
  setting is kept while you edit and run again. (The fluid used to be opaque, and a body inside a box of fluid was hidden by it.)
- **Dragging a model out of the call it is in always works.** `select_surface(cube, ...)` dragged out of its selection used to be refused when the place it was dropped at would have put its definition below another statement that uses it
  (the cube is used by every selection of it): the whole drag failed, so nothing could be taken out. The model is now taken out of the call (`select_surface(..., seed=...)`, a placeholder where it was) and stays where its definition is when
  the place cannot be had; **the other statements that use it hold a reference to it** (`# shadow: cube`), so the model stands at the top level instead of moving under the next statement that uses it (it used to land in
  another selection, nested again). Only a plain move that takes nothing out of a call is still refused, with the reason.

- **A model taken out of any call that needs it leaves a placeholder, whatever the call is.** Taking one of the two models out of `difference(a, b)` left `difference(a)`, which is an error, because the tree only knew that
  `difference` takes any number of models. It now reads how many a call needs from the function's own signature (`difference(a, b, *rest)` needs two, `union(a, *rest)` one, a function that takes any number of models at least one): a model
  leaves a call that keeps what it needs (`difference(a, b, c)` loses `b` and stays `difference(a, c)`), and leaves a placeholder in one that would not (`difference(a, ...)`). Nothing is listed by hand, so an operation that is added later,
  in the library or in a block of yours, is covered as soon as it is written.
- **A locked model that is selected for an operation of the menu is a reference in it, as when it is dragged.** `union` of a model and a locked one is written `union_1 = union(a, c)  # shadow: c`, below the locked model's `lock(...)` line,
  so the locked model stays where it is, shows at the top level, and is not made part of the operation.

### `fluid_analysis` is the flow around a body: the same inputs as `flow_topology_optimization`
- **`fluid_analysis(body, domain, inlets, outlets, boundaries, fluid=water, ...)`**: the flow goes around a body, inside a domain, and the analysis takes what the optimisation takes -- it is to `flow_topology_optimization` what
  `static_analysis` is to `topology_optimization`: the same flow, solved once for the body as it is. It used to be given only the fluid (`fluid_analysis(difference(slab, post), ...)`), so there was no body for the flow to go around
  and it could not be the analysis of what the optimiser designs. The old form is gone: `fluid_analysis(post, slab, ...)`. The result shows the fluid coloured by the speed with the body solid, `.wall_force` is the force on the walls (the body's: its drag),
  and `.body` and `.domain` are what it was made of. The menu writes `body=...` and `domain=...` as placeholders, as for the optimisation; example 16 and the docs are migrated.

### One look for what a plane shows: the field viewer is drawn like the section view
- **The field viewer uses the section view's colours and dark lines.** It had its own rainbow; now a field is blue, cream and orange on the same stops as the section view, with the same dark lines at round steps of its values, and a heavier
  line where it is zero. A field with both signs is cream at its zero (so `field_from_body(part)` reads exactly as the section does: the surface cream, with its dark contour), blue below and orange above; any other field runs from blue at its lowest
  value to orange at its highest. The colour bar, the 2D view and the viewport's disc all agree. (A part coloured by a result, `colored(...)`, keeps the map it is given.)

### The tour works again, and a point is only its gizmo
- **A point is not drawn at all.** No ball, nothing to pick in the viewport: select it (in the tree) and its **gizmo** shows where it is, and moves it. A point keeps its place, its numbers and its handles line, and goes wherever a position goes
  (`distance_to_point(anchor)`). `point(x, y, z, size=...)` lost its `size`: there is no ball to give a radius to.
- **Selecting a point (or a field made of one) no longer zooms the camera into it.** The camera framed the point's box, a fraction of a millimetre wide, and everything else went out of view ("0.2 mm" on the scale bar); a point never moves the camera.
- **The first menu of a point offers `distance_to_point`**: the field of the distance from the selected point (also in *Add operation* with a point selected). It is only there for a point.
- **The Render settings rows have no bin any more** (the resolution and the quality could be deleted from the tree): a render setting is a line of the code, changed or deleted there; a click on a row goes to the line.
- **The guided tour's Render settings step is done in the code editor**: it asked for the resolution to be changed in the tree, where it is read-only now, and the rest of the window was blocked. The framed line is the one to change, and *Show me* types it.
- **The tour's Placeholders step shows a placeholder** (`washer = offset(drilled, ...)`, amber, with its row and the note under the tree) and puts it away when the step is left.
- **A new tour step, *Move the point***: the point of the field is shown, selected with its gizmo, and the growth follows it as it is dragged.
- **What a finished tour step says stays on the card** until *Next* is pressed: it moved on by itself after a second, before the note could be read.

## 0.6.0 — one import function, a field walk for surface selection, materials and conditions as models

### Every kind of boundary condition is an input of its own; every condition is a model of its own
- **One required input for each kind of condition**, for every analysis: `static_analysis(part, supports, loads, ...)`, `modal_analysis(part, supports, ...)`, `topology_optimization(part, supports, loads, ...)`,
  `thermal_analysis(part, fixed_temperatures, heat_inputs, heat_generations, convections, ...)`, `fluid_analysis(domain, inlets, outlets, boundaries, ...)` and the two optimisations of heat and flow. A kind the analysis has none of
  is `[]`. A menu writes the analysis with **a placeholder for each of them** (`inlets=...`, `outlets=...`, `boundaries=...`), so every kind is in front of you. A condition in the wrong input says where it goes
  (`supports=[force(...)]`: "supports are fixed(...) items, and a force(...) is not one -- it goes in loads=").
- **`static_boundary_conditions` is gone** (no body in it, no set): the supports and loads are given to the analysis, and each is a model of its own. Scripts that used it fail with a `NameError`; the examples are migrated.
- **A "boundary conditions" row under every simulation in the model tree**, with a placeholder for each kind it still waits for and its conditions under them. **Its eye shows or hides all of them at once**; every condition keeps
  its own eye and **renders on its own**, the same everywhere: the surface it acts on in its colour, the pads or arrows, the label ("20 °C", "Inlet 10 mm/s", "Wall", "Slip" ...) and the legend. New colours for temperatures, convections, heat generated, inlets, outlets, walls and slips.
- **Dragging several conditions at once works.** Select them (Ctrl or Shift) and drop them on a placeholder, on the simulation or on its *boundary conditions* row: they all go into the input of their kind as a list (`slips=[slip_1, slip_2]`); a list that is there gets them as more items; **conditions of different kinds dropped on the simulation
  or its row each go into their own input, in one edit** (undone by one Ctrl+Z); on a placeholder only its own kind is taken, another kind is refused with the reason. A condition defined below the simulation is moved above it.
- **No body in a condition, ever**: the menus' **New support or load**, **New thermal condition** and **New flow condition** take the surfaces, fields or bodies they act on -- the simulation is given the body.
- **A flow that has nowhere to go says so before it solves**: an inlet with no outlet and no moving wall stops with "give the flow an outlet" instead of iterating until it diverges.
- **Locked models are still draggable, but a drag never moves them: it always makes a shadow reference** (as with Ctrl). Locking is how you say that a model is read-only; a reference to it is allowed.
- **A condition shown after the body was already shown now appears.** The viewport matched shapes by their geometry alone, so a body tinted where a condition acts (the same geometry, other colours) was taken for the body itself and dropped.
- The model tree's card is a little wider by default (360) so that "boundary conditions" fits.
- **The flow optimiser makes pointed bodies again, and stops when it is done.** The step it moved the boundary by had become a cap that shrank as the run went, and shrank again whenever the objective did not fall as predicted
  (noisy sensitivities made that every few iterations): by the tenth iteration the boundary moved 0.1-0.3 mm at a time, the nose and the tail never got where they were going -- a round, stubby body -- and the
  objective crept down 0.1 % an iteration to the iteration limit with nothing to see. The cap is now one element at most (half an element when 48 or more iterations are allowed) the whole way, and a step that makes the
  objective worse is taken back and halved, so it never rises. **The run stops when the objective has stopped improving** (less than half a percent over the last five designs kept). The example (a round post, 24 iterations asked): the real drag
  of the final body 4.0e-7 N (it was 4.5), the body 22 mm long with pointed ends (it was 19, blunt), stopped after 22 iterations instead of running to the limit; a square post nobody tuned for: 4.1e-7 (it was 4.4), 22 iterations of the 30 allowed.
- **The flow optimiser keeps a symmetric problem symmetric** (`symmetry='auto'`, as in `topology_optimization`). A symmetric domain with a symmetric body and symmetric conditions came out with a crooked nose: the mesh is never
  exactly mirror-symmetric, the difference grew with the iterations, and one flank ran ahead of the other. When the domain, the body, the regions and the boundary conditions are symmetric about a plane through the middle of the domain,
  the sensitivities and the level set are averaged with their mirror images every iteration, and the output says so. `symmetry=None` leaves it alone, `'y'` / `'xz'` / `{'y': 20.0}` ask for planes. On the example
  the flanks of the final body agree to 0.12 mm, the real drag is 4.08e-7 N, `result.symmetry` says what was used.
- **`walls` and `slips` are one input, `boundaries`**: a flow has wall(...) items, slip(...) items or both (`boundaries=[wall_1, slip_1, slip_2]`) -- a flow with only slips no longer needs a `walls=[]` beside them. The old keywords are refused with that reason.
- **The Simulation menu is never greyed out for lack of something.** Right-click the empty viewport, or select only conditions or only a material, and every simulation is written with a placeholder for the part and everything else that is missing.
  (An entry is greyed out only when the selection has something it does not take: a thermal condition for *static_analysis*, a surface or a point for any of them, a second part or material for an input that takes one.) **The menu now lists the two optimisations, `thermal_topology_optimization` and `flow_topology_optimization`**, which it never did;
  *fluid_analysis* is a plain analysis.
- **Several selected rows now survive a press and hold.** Pressing the mouse on one of several selected rows of the model tree cut the selection down to that row at the first tremor of the hand
  (the tree does its own drag, so Qt took the slightest move for the start of a rubber band): a drag of four slips onto `boundaries=...` carried one, and `boundaries=[slip_1]` was all that came of it. A selection now changes only when
  the button comes up without a drag; a drag carries everything selected (`boundaries=[slip_1, slip_2, slip_3, slip_4]`).
- **`max_iterations` is gone from every simulation** (static, modal, thermal, topology optimisation, thermal optimisation, flow). It was the equation solver's own step limit -- 20000, 50000, 60 --
  which a problem that has a solution never reaches, so it did nothing that anyone could see, and next to `iterations` (the design updates) it only confused. The solvers stop when the residual is below `tolerance`
  and say so when they cannot get there. A call that still gives it is refused with that reason.
- **Deleting a model that a call is given by keyword rewrites the call** (`boundaries=[slip_1, slip_2]`, `material=steel_1`): the model leaves the list and the call keeps the rest, and when it was the last one the input is a placeholder again (`slips=...`). It used to leave a `NameError`.
- **An old call that gives all the conditions as one list says why it does not work**: "every kind of boundary condition is an input of its own, and heat_inputs, convections were not given ...", instead of Python's "missing 3 required positional arguments".
- A condition shown as the last statement prints what it is (`outlet 0 MPa`), not its place in memory; conditions have no gizmo even before they have run; selecting several conditions no longer says that one gizmo moves them.

### Errors are red, placeholders orange, edits do not lose the solver
- **Errors are red, what waits for you is orange.** The frame, the banner, the card's header and its note of an error are red; a placeholder (the row, the note under the tree, the line in the editor) is orange. An error also **goes the moment the script is edited** (what the run of the new text finds out comes back after a moment of quiet): it no longer stays on screen through a run that takes a minute.
- **An edit no longer waits for the whole solver, and no longer loses it.** An edit at or above the statement that runs (its call, a line above it) stops a running optimisation at once; an edit below it (an eye toggled under it) lets it finish. A solution that was whole when the edit arrived is kept, so the next run does not start the optimisation again.
- **Typing a bracket over a selected placeholder replaces it** (`vector=...` + `(0, -100, 0)` is `vector=(0, -100, 0)`, it used to give `vector=(...)0, -100, 0)`).
- **Deleting a model that others are made of leaves a placeholder in them** (`move(..., v=(5, 0, 0))`; a list loses it) instead of a `NameError` that says nothing about what to do. Undo brings it all back.
- **The render region is no longer rewritten from a guess.** A new model that reaches out of the region makes the region grow -- but while the run that makes it was still on its way, the region it sets was not known yet, the default one was taken instead, and `view.set_bounds(...)` was replaced by a smaller region of its own. It now waits for the run to be done.
- **A surface selection gets no `handles(...)` line** (and an analysis neither): the tree did not know before the first run that they cannot be moved.
- Automation: `seltext <text>` selects the first place of the script with that text (what a click does), for the tests that type like a hand.
### A run that fails still draws what ran
- **A placeholder no longer stops the run: it blocks only its own statement.** `force(region=..., vector=...)` written by a menu used to end the run at its line -- the part
  (displayed below it) and the render settings (below it too) were never reached, so the viewport was empty and the region was the default one. Now the statement with a placeholder is
  skipped, and so is every statement that uses a name it would have made (`force_1`, the conditions made of it, the simulation made of those); **everything else runs and is drawn**.
  The line that waits is marked as before, the tree lists what waits in orange, and the output says which line is waiting for what.
- **No `handles(...)` line is written under an analysis or an optimisation any more.** The tree predicts what a statement that has not run makes; it knew a new `topology_optimization(...)` was a simulation but not that its result cannot be moved, so the first selection wrote the gizmo line under it -- and the run then failed on it (below). It is a result from the moment it is written.
- **When a line of the script fails, everything that ran before it is drawn** -- the shapes, the boundary conditions, the results of the lines above the error -- and the error is shown beside the picture. Until now a run that failed drew nothing at all and left the last picture that worked (a file that had never run cleanly showed nothing; the live picture of an optimisation was the only thing on screen). A script that does not parse ran nothing, so its picture stays as it was.
- **`handles()` on the result of an analysis or an optimisation** (a line `result = handles(result, ...)`) **no longer fails** with "Cannot convert Topology optimization ... into a Shape": a result has no surface of its own to move, so it is the result itself when nothing is placed, and a message says why when something is.
- **A menu never writes a number or a text of its own into a call.** The simulation entries of a model's menu used to write `box_exact(...)` regions on the ends of its bounds, and others a made-up load (`vector=(0, -100, 0)`), temperature, offset distance, element size ...: now **what a call cannot do without is a named placeholder** (`region=...`, `vector=...`, `value=...`, `o=...`, `nx=...`) **and every other argument has the library's own default** -- exactly what the completion writes. The menu writes the models you selected and nothing built for you: `static_analysis(part, conditions=...)`, not conditions of its own; the numbers and the conditions are for you to say, in the script. (Shapes made at a click -- a box, a sphere -- keep the place and size of the click.) The transforms (`move`, `rotate_*`, `scale_*`, `reflect_*`) now show their real arguments to the completion and the hover.

### Code first: nothing is asked, everything is written, and what is missing is a named placeholder
- **The popups that asked for arguments are gone.** A menu entry no longer opens a form for the force, the volume fraction, the element size ...: it writes the call with **every argument it has --
  what is missing as a placeholder after the argument's name, the rest with its default** -- and the numbers are changed in the code editor, where the call is. A `force` is written
  `force(region=..., vector=(0, -100, 0), profile=None)`; a topology optimisation with its volume fraction, element size, iterations, penalty, sharpness ... (the visual jobs stay visual: dragging a surface or
  a gizmo, picking a surface with *Select Surface*, which keeps its mode, angle and radius -- it is a tool for picking an area, and shows the patch as you set them).
- **The completion writes the same call.** One function (`fieldes.completion.complete_call`) writes a call for the right-click menus and for the completion list alike: placeholders after their
  names for what a call cannot do without (`select_surface(shape=..., seed=..., angle=15.0, mode='flat', ...)`), every other argument with its default, and **one argument to a line** when the call does not fit on one (88 columns),
  nested calls and lists laid out the same way. `Tab` goes to the next placeholder. **Hover an argument's name and the documentation says what it is** (with its default). New in Edit > Lines:
  **Split the call over lines** (`Ctrl+Alt+Enter`) and **Join the call onto one line** (`Ctrl+Alt+J`) for a call that is already in a script.
- **Placeholders are amber, not red** (they are no error: they are what has to be done for the script to run) -- in the editor, in the tree, and in the note **under the model tree, which lists them (statement and argument)
  and goes to the one you click, selected in the code editor**. The rows of placeholders in the tree are named by their argument.
- **The condition functions take named arguments**, so that a placeholder has a name to carry: `force(region=, vector=)`, `fixed_temperature(region=, value=)`, `heat_input(region=, watts=)`, `heat_generation(region=, watts=)`,
  `convection(region=, coefficient=, ambient=)` (the positional forms work as before), and the menus write the named forms.
- **The render settings are read in the tree and written in the code editor.** The Render settings row shows the region, the resolution and the quality as read-only rows (a click goes to the line); the number fields are gone.
  (The region still grows by itself when a model comes out of it: that is the program writing code, not typing numbers.)
- **`center(body)` and `bounding_box(body)`** -- the middle of a body's box as a point, and the smallest axis-aligned box that holds it as a body -- are library functions, and the first two entries of the right-click menu of a body.
  The numbers are the extent the body knows (a box, an imported part) or the one found by searching its field, rounded to the shortest number within the search's tolerance: a box 60 long says 60.
- **A number you change in a call wins.** The numbers of a shape that has been dragged are kept in a list under its call (`expose(...)`), and editing the `15` of `cylinder(15, ...)` changed nothing: the list still said what the shape was.
  A run that finds a number of the call changed while the list is as it was now takes the number of the call, and the editor rewrites the list to say so (one `Ctrl+Z`); dragging, and editing the list, work as before.
- **The card of the optimisation that runs is a card like the others**: dragged by any empty place, resized from its edges, with the same margin on every side (the graph fills what is left), and kept where you left it.
- **Hiding and showing is fast again**: every run searched the extent of the part again for each `select_surface` (a third of a second or more each, three of them on a small bracket: 2.5 s a click); the search is remembered by what the shape is made of
  (a click on an eye of a small script: 2.7 s -> 0.01 s of script).
- **The legend of a result is there only when the result is on screen.** The colour bar of an optimisation (and its step controls) stayed when nothing of it was drawn -- while the run started over, or after its eye was turned off. It follows what is
  drawn now; and **the picture of an optimisation that runs is drawn only if its eye is on** (turn it off while it runs and the picture and its legend go at once; the card still says how it is doing).
- The tour's render-settings step and its placeholder step say how it is now.


### Conditions are drawn as their surfaces and symbols only; the menu makes no box of its own
- **A condition made from the menu with nothing to say where it acts has a placeholder there, not a box of the menu's own.** Every **New support or load / thermal condition / flow condition** entry used to write a 5 mm
  `box_exact` region at the cursor as a model of its own and use it (`force_2_region = box_exact(...)`, `force_2 = force(force_2_region, (0, -100, 0))`); a box nobody had asked for was then a region of the conditions
  and showed as a patch. Now it writes `force_1 = force(..., (0, -100, 0))`, like every call that is missing an argument: that statement waits for the placeholder, and a body, a field or a surface is dropped on its
  row of the model tree (a region can be a body as well as a surface). With a body and regions selected, or a surface or field alone, the condition is made of them as before. (The simulations'
  first `static_boundary_conditions` boxes on the ends of the model are unchanged.)
- **A set of conditions draws only what it is: the held and loaded surfaces, flat in their colour, with the pads and arrows on them.** It no longer draws a copy of the whole body (the body has its own row: show it
  with its eye to see both), so two sets shown together cannot fight over the same surface. A set that no simulation has been given used to draw its *regions* instead -- meshed boxes, and thin layers
  that came out speckled -- because it had no body to draw on; it now finds the body their surfaces were picked on (`select_surface`, `surface_from_bodies`), else the biggest solid of the script that the regions
  reach, and draws exactly the picture of a set used by an analysis. Pads are kept inside their face (they used to hang over the edge of a narrow one).

- **A stop is no error.** Hiding or showing something while a script runs stops that run (an empty `Exception` raised in it) and starts the next; the stop was shown as "Error in line N: Exception" on the
  line that was running (an optimisation's, say). A run stopped by an edit now shows nothing -- the picture of the last finished run stays until the next one is done.
- **Hide and show no longer pay for the symbols of the conditions again.** Where the pads and arrows of a set of conditions go is found by evaluating the part at thousands of points, and every run did
  it again, for hidden sets too; it is remembered now by what it is made of (the part, the region, the numbers). A run that changes none of them reads it back (measured on a bracket with two sets: 9.7 s the
  first time, 0.23 s after a hide). An edit made while an optimisation runs still waits for the solver to hand control back before the next run starts.
- **The picture of a running topology optimisation is shown for a part that takes long to mesh, and shown fast.** A new picture is published every iteration, and each one threw away the render of the one before
  it before it was done -- a bracket (an imported surface, cut by cylinders) takes seconds to mesh from its field, longer than an iteration, so nothing was ever shown (only the card). A render in progress now
  finishes and is shown, and the newest picture is meshed next. The picture is also no longer meshed from the field at all: the optimiser hands over the surface of the design itself (the density surface
  of its own mesh, as the iteration steps use), drawn as it is and coloured by the density -- the bracket shows from the first iterations.

### Menu entries a selection cannot make are greyed out, and conditions made of a multi-selection keep every model
- **Selecting a force and a fixed support and choosing *static_boundary_conditions* wrote a call without the force** (and sometimes opened an input form): the first model selected was taken to be the body whatever it was,
  so a force or a support selected first was dropped from the call. A set of conditions, a material, a support or a load selected first is now what it is -- the call keeps all of them, in whatever order they were
  selected (`static_boundary_conditions(supports=[fixed_1], loads=[force_1])`), and a simulation takes the part from among the other models.
- **An entry the selection does not fit is greyed out, with no message.** Every selected model is classified once (part, profile, field, surface, point, lattice cell, result, support, load, set of conditions, thermal or
  flow condition, material, fluid) and every entry declares what it takes (`menu_catalog._TAKES`: signatures of accepted kinds and needed kinds); the one check compares them for every entry of the Operation and Simulation
  menus and for the conditions made of several models, so there is no rule per operation. A part selected with supports and loads cannot make a set of boundary conditions (the part is given to the simulation);
  *move*, *offset* ... are greyed out on a force or a material; simulations without a part, a *modal_analysis* with a load, a *thermal_analysis* with a structural support are too. A model that has not run yet fits everything;
  custom blocks are always offered. The check of the whole menu takes about 3 ms.

### Both optimisers set their own step: no `move`
- **`move` is gone from `topology_optimization` and `flow_topology_optimization`.** It was a fixed cap on how far a density (or the flow body's boundary) may change in one iteration -- the same step whether the
  optimisation had 10 iterations or 100 -- so a short run could never go the whole way. Now **`iterations` sets the pace**: the step starts large (the fewer the iterations, the larger: up to 0.5 of a density,
  up to one element of boundary), its cap falls towards the last iteration so that the design has settled by then, and within that cap it follows how the design improves, like a trust region -- the change
  that the sensitivities predict for the step taken is compared with the change the next iteration shows (the compliance, the drag): it grows while the objective falls as predicted, shrinks when it falls
  much less or rises. The flow optimiser's step that made the objective worse is still taken back and halved. The voxel (`element='hex'`) and thermal optimisations keep their fixed step.
  A script that passes `move=` gets an error that says it is not an argument.
- **The sharpening of the topology optimisation is tied to `iterations` as well**: it reaches its full steepness at three quarters of the run, however long it is. It used to start at the 10th iteration
  and step every 5th at least, so a run of 30 iterations or fewer never finished sharpening and its design stayed grey; now even 18 iterations end crisp (the optimiser's own compliance after 30 iterations
  is 4.55, what 60 or 100 iterations reach, against 4.83 before).
- **What was measured** (a beam, 40 % of the volume kept, 10 / 18 / 30 iterations, the stiffness of the cut part, lower is stiffer): original 5.71 / 5.62 / 5.63, now 5.78 / 5.35 / 5.26 -- within the noise at 10,
  5-6 % stiffer at 18 and 30; the step control alone changed nothing that the noise (the volumes of the cut parts differ by 1-2 %) does not explain, because the step is rarely what limits an iteration of a topology
  optimisation. The flow optimiser gained a little (12 iterations: drag -31.0 % against -29.9 %, one step taken back against two, a take-back being a wasted flow solve).

### The live views look like the result; the flow optimisation draws itself; stepping through iterations is quick
- **What an optimisation draws while it runs is its result's own picture.** The optimisers hand the viewer the shapes the result is made of -- the topology optimisation the part cut from the density
  (coloured by it, with the colour bar and the result card), the flow optimisation the fluid around the body coloured by the speed of that iteration's flow, with the streamlines from the inlets and the
  particles moving along them, and the body solid -- and the viewer renders them with the code that renders the result, in the render settings the script has given so far (they are read from it while it
  runs); a new picture replaces the one shown when it has its meshes, so nothing blinks. A first version drew triangles of the mesh itself: jagged edges, no flow lines.
- **The flow topology optimisation draws itself** (it showed nothing until it had finished): the body after every accepted iteration, with a card with the drag (or objective) and its graph, the body's volume
  against what is allowed, and how far the boundary moved. A first version drew the fluid instead of the body (this level set is positive inside the body): fixed.
- **Stepping through the iterations of a tetrahedral topology optimisation is quicker.** The result card used to mesh the density field of each iteration the first time it was shown (0.23-0.36 s each on a small
  part, much more on a fine one); the surface of each iteration is now made from the optimisation's own densities (exactly where the optimised part is cut) in a thread of its own as soon as the result is there --
  also when the optimisation was read back from the result cache -- and the viewer draws those triangles. The field stays what sections and probes read, and the last step is still the real mesh.

### Watching a topology optimisation as it runs
- **The viewport shows the design after every iteration.** A long optimisation used to show nothing until it had finished, so there was no telling whether it was doing something useful or the input was off. Now the
  kernel puts a picture of the design after each iteration (`run_progress.liveView`: the part where the density is above the level that keeps the volume asked for -- marching tetrahedra through the mesh, and the faces of
  its boundary cut where the density crosses the level) and the viewport draws it in place of the pictures of the run before, camera framed on it when nothing was drawn yet. It is only a picture: nothing is computed from it, and
  it goes when the optimisation ends and the result takes its place.
- **A card at the top says how it goes**: the iteration of the most there will be, the compliance and its change since the first iteration with a small graph of every iteration so far, the material the design has against
  what was asked for, and the biggest change of a density in the iteration. A compliance that does not fall, a part that stays whole or goes empty, point at the input (supports, loads, volume fraction, element size).
  Tetrahedral optimisations only: the voxel (`element='hex'`) and flow optimisations do not draw themselves yet.
- **`topology_optimization` documents `iterations`**, the number of design updates (the equation solver's own step limit is no argument: see `max_iterations` below).

### The model tree is read from the script, not made by running it
- **The tree shows what the code says, always.** It used to be built from what the statements of a run had made, so while a script was calculating, stuck, or stopped at a breakpoint or a
  placeholder, the tree showed only what had been made up to that point; and a line whose assignment threw an error left a "ghost" (a variable the tree knew, with no model behind it). Now the tree is
  read from the **text** of the script (`python/fieldes/outline.py`), a moment after the last key: every statement that makes a model has its row, whatever the run is doing. A statement that
  threw an error has a red cross row and the error in the note; the rows below it stay. A line that does not parse leaves the tree as it was.
- **What a run adds is laid over the rows, never a row of its own.** The exact kind of what a statement made, its extent (what the viewport frames and the region of interest is made from), whether it can
  be dragged, are remembered statement by statement as the run goes, and for a statement that was edited, what its function made before. A statement that never ran has its kind guessed from its
  function (the menus' groups, what the library declares, name patterns): `time.sleep(25)`, a module's function or a name defined nowhere is no model.
- **No more waiting for a run to edit the tree.** The edits of the tree (drag, remove, replace, the eye and the lock) work from the rows of the text, so they work while a script runs. The note
  "Rows: the last run that worked" is gone; a script whose text cannot be read says so when an edit of the tree is tried.
- **Placeholders are announced.** A note under the tree says which statements wait for something (`A placeholder (...) waits in thick: ...`, with what to do), as does each placeholder row's tooltip.
- **The last model of any list leaves a placeholder, not an empty list**: `loads=[force_1]` without `force_1` is `loads=[...]` (an empty `[]` would only fail later, when the analysis runs).
- **`convection(region, h, ambient=20)` works again.** The `ambient=` keyword that the documentation, the example and the error message all name had been lost when the function took any number of regions.

### Placeholders instead of deletion, replacing by dropping, completion with placeholders
- **A model taken out of a call that cannot do without it no longer deletes the call.** The old rule -- the call is deleted, after a question that said why, and what is made of it loses it in turn -- was
  in the way of every move in the tree (the call a model is dragged out of, a model dropped on another). Now **a placeholder, `...`, takes the model's place**: `thick = offset(..., 1.0)`. Nothing is
  deleted, nothing is asked (the question and its "do not show again" are gone, with the rule), and `Ctrl+Z` undoes it. Operations that take any number of models still just lose the argument, and a
  model in a list keeps the rest of the list.
- **A statement with a placeholder does not run and does not throw an error: it waits** (and what is made from it; the rest of the script runs, see "A placeholder no longer stops the run"), and says so --
  *Line N is waiting for what goes in place of its placeholder (...)*. **Continue is not offered** (F8 and the button do nothing there; the pause button is
  off); the statement runs by itself once every placeholder in it is filled in, since every edit runs the script again. `...` that is no argument (a stub function's body, `x[..., 0]`) is plain Python and does not stop anything.
  Placeholders are drawn bold red in the editor.
- **The model tree keeps the statement and shows its placeholders**: a **placeholder row** under it for each `...` (what is below the stop is not built yet, so its rows wait, as at a breakpoint).
  **Dropping any model on a placeholder row replaces it at once**, no menu; dropping on the statement's row fills its first one. One `Ctrl+Z` undoes it.
- **Replacing a model by dropping a model of its kind on it, without a question.** Drag a model onto one of a call's arguments that is of its kind -- a part on a part, a force on a force, a field on a
  field, a support on a support -- and it takes that one's place in the call; the replaced model is not deleted (it goes out to the top level, or to the next statement that uses it). Dropped on a
  **shadow**, only that reference goes. The dragged model leaves its old call as always (a placeholder there if that call needs it; `Ctrl` makes a copy). Dropping on an operation that takes the model
  in as an argument still adds it to the operation.
- **Completion and the creation menus write placeholders.** A function picked from the completion list is written with a `...` for each argument it cannot do without, the first selected so that
  typing replaces it, `Tab` going on to the next; an operation created from a menu with no model selected is written with `...` for its model, instead of being refused.

### Model tree: a model taken out of a list does not delete the call
- **Deleting `force_1` from `static_boundary_conditions_2` (with `force_2` still in it) deleted the whole set of conditions.** The tree takes a model out of a call by editing the call, and it knew how
  only for a model written as a plain argument (`union(a, b)`) or in a list written positionally (`union_all([a, b])`); a model inside `loads=[force_1, force_2]` -- a list given as a *keyword* -- counted as "used inside an
  expression", which a call "cannot do without", so the statement went (after a question, which *Do not show this message again* had turned off, hence no warning). Now a model that is one of the elements of a
  list the call is given -- positional or keyword, also a list inside a list -- is taken out of that list and the call keeps the rest: `loads=[force_2]`. The same holds for `topology_optimization(part, [push, pull])`,
  where taking out `pull` used to delete the whole optimisation. When the last model of a list goes it leaves a placeholder (`loads=[...]`): the conditions and their supports stay and the script stops at the placeholder until
  something is written. A list inside a list goes whole when its last model goes, and a tuple of one element keeps its comma. A call given one *positional* list of models still cannot lose the
  last one, and the fixed-input operations (`offset(plate, 1.0)`) still go with the model they work on, after the question: checked in the real window, as are `union(a, b)` and `union_all([a, b, c])`.

### Editor: typing next to a breakpoint, pause / run again / terminate, breakpoints that are kept
- **Breakpoints are bound to the code on their line, not to the line's number.** A breakpoint used to be a text cursor at the start of its line, which got three edits wrong: deleting
  its line put it on the statement below (a different one); `Enter` at the start of its line left it on the new blank line while the code went down; moving a line down (`Alt+↓`) left it behind.
  Now it follows the code: deleting the line removes the breakpoint (undo brings it back, and a line cut and pasted elsewhere takes its breakpoint along), `Enter` or a pasted line in front of the code
  moves it down with the code, moving a line moves the breakpoint with it, and typing in the line -- even clearing its text -- leaves it on the line.
- **A breakpoint no longer takes the cursor away.** Every edit runs the script again, a run that reaches a breakpoint stops there, and the stop used to move the text cursor to its line --
  after every key, so typing anywhere else was impossible. The cursor now stays where it is; the view follows the stop only when the script is not the widget in use (after `F8`, say, with
  the viewport clicked last).
- **Three buttons at the bottom right of the editor: pause / continue, run again, terminate** (also under **Edit**; no button says "Stop").
  - **⏸ / ▶ pause / continue** (`F6`): the running script *freezes* where it is and goes on from there (nothing is lost or recomputed). It waits at the next *checkpoint* -- every place that
    reports progress: an iteration of a solver or an optimisation, a step of a STEP import, the start of the next statement. (Freezing a thread at an arbitrary instant is not safe: it
    may hold the memory allocator's or the interpreter's lock and take the whole program with it.) The bar says *Pausing...* until the script has reached one, then *Paused*; a step that
    reports nothing (a long meshing) runs to its end first, and a script that ends before it reaches one simply ends. At a breakpoint the button is ▶ and does what `F8` does.
    Editing a paused script lets it go on, to end, and starts the new run.
  - **↻ run again** (`Ctrl+Shift+F5`): the script runs again from the beginning; a run in flight, or paused, is terminated first.
  - **● terminate** (red dot, `Shift+F5`): the long solvers -- static, modal, thermal and flow analyses, the structural, thermal and flow topology optimisations -- look at a termination flag as they iterate and
    give up ("cancelled") within an iteration; the script itself is interrupted with an exception that a script's own `except Exception` does not swallow (sent again every second, up to five times).
    Nothing of a terminated run is shown -- the viewport keeps what the last run that finished made -- and the result line says the script was terminated (it is not reported as an error of
    the script). **Limit:** a STEP or mesh import, the cell layout of a lattice and a meshing are not interruptible: they finish first, and the bar says *Terminating the script...* until they have.
- **Breakpoints are remembered per file.** They are written down (in the user's settings, with the text on their lines) at every change and at the end of the program, and come back when the
  file is opened again -- also after Revert and after the file was changed outside FielDes: each one goes to the line that still holds its text, the nearest within 30 lines of where it was,
  or is dropped when the text is gone. Save as takes them along to the new name. The last 300 files are kept.

### Boundary conditions without a body, several sets of them, and a symmetric design for a symmetric part
- **`static_boundary_conditions(supports=[...], loads=[...])` takes no body.** The supports and loads are described on their own, and the **simulation is given the body**,
  as its first argument: `static_analysis(part, conditions)`, `modal_analysis(part, conditions)`, `topology_optimization(part, conditions)`. One set of
  conditions fits any simulation, and several sets given to one optimisation cannot disagree about "their" body. The old form, with the body in front, is refused with a message that says
  why and what to remove. **The conditions are drawn on the body of the simulation that uses them** (blue pads, red arrows, as before); conditions no simulation
  uses are drawn as the regions themselves. The menus write the new form (**Simulation -> static_analysis** with a body and a set of conditions selected makes the analysis of the body; a set
  alone says it needs its body), and the model tree no longer has a body to drop on a set of conditions. Migrate: `static_boundary_conditions(part, supports=[...])` -> `static_boundary_conditions(supports=[...])`.
- **`topology_optimization(part, [push, pull], ...)`: several sets of boundary conditions, optimised against all at once.** Each set has **its own supports and loads** (a part that is pushed and
  pulled; held one way in one use and another way in another); the sum of their compliances is minimised. Dropping a second set on an optimisation in the model tree makes the list; selecting the
  body with several sets and choosing **Simulation -> topology_optimization** writes it. (Several load cases of one set, `loads=[[...], [...]]`, still work.) A set that holds the part nowhere, or one
  whose loads cannot act, is refused with the set's number.
- **A symmetric problem gets a symmetric design.** A bracket test -- an exactly symmetric part, loads that are mirror images of each other -- ended up with a strut on one side and none on the other: the
  optimiser does not keep a symmetric problem symmetric (the mesh of a symmetric part is never exactly symmetric, the small difference grows round two members that do the same job, and one takes the
  other's material -- the asymmetry of the material grew from 0.4 % to 13 % over the run). New `symmetry='auto'` (default): the planes through the middle of the part (x, y, z) are tried; when the part, the supports,
  the loads (what acts on one side is what acts on the other, mirrored) and the keep / avoid regions are all symmetric about one, the sensitivities are averaged over the mirror pairs, so the design stays symmetric
  (the output says about which plane; on your bracket the two halves differ by 2 % instead of 13 %). `symmetry=None` turns it off, `'z'` or `{'z': 0.0}` asks for a plane whatever the loads.
  Compliance of a symmetric design is a little higher than that of the lucky asymmetric one (3.16 against 3.09 on the bracket): that is the price of the symmetry you asked for.
- **Loose material is taken out of the design as the optimisation goes.** From the tenth iteration on, material (density above 0.3) that no chain of elements sharing faces joins to a support, a load or a
  region to keep is removed -- it carries nothing, and used the volume of a member that would be of use. On your bracket a few elements at a time were found (most before iteration 50). What is left
  of the "useless" material in the final design -- thin, grey fragments (density 0.3-0.5) along the outer edges, where a strut from the wall's corner to the ring would go -- is attached to the design and
  sits at the margin of being useful: it neither grows nor goes. With `symmetry` it is the same on both sides.

### Seven languages, and an installer (0.6.0)
- **FielDes speaks English, Spanish, French, German, Japanese, Simplified Chinese and Russian.** The menus, dialogs, tooltips, notes and questions, the guided tour, the
  guide, the creation forms, the result legends and step labels, and the words Qt itself shows (*Yes*, *Cancel*, *Copy*, *Paste*) -- about 940 texts. The language is the one of
  Windows (the first of the computer's preferred languages that FielDes has), or the one chosen under **Settings → Language**, which offers **Restart now**
  (the language is read when the program starts). The English text of the program is the key of each translation, so a text that has none reads in English: nothing is ever blank.
  The translations are plain files, `translations/<code>.json`; `translations/README.md` says how to improve one or add a language, and
  `python scripts/translations.py check` lists what a file lacks and the texts whose `%1` places differ from the English.
  **Stays in English:** the names of functions and arguments (they are code), the errors and notes the Python library prints about a script, the written documentation
  (`docs/`) and the command line help.
- **A Windows installer**, `FielDes-0.6.0-windows-x64-setup.exe`, for people who would rather not unpack a zip: it asks for the **language** (Windows' by default) and the folder,
  installs **for the current user** (no administrator rights, `%LOCALAPPDATA%\Programs\FielDes`), makes the Start menu shortcut (and a desktop one if you want), the entry in
  **Settings → Apps** with its uninstaller, sets the program's language, and starts FielDes. Installing a newer version over an old one replaces the program's files and
  leaves yours. `/S` installs silently (`/language=de /dir=… /nodesktop /nolaunch`). It is **not code-signed**, so Windows SmartScreen says "unknown publisher" the first time
  (*More info → Run anyway*). Built by `installer\build-installer.ps1` (the compiler of the Visual Studio Build Tools, nothing to download). The portable zip stays.
- The version is 0.6.0 (one place: `CMakeLists.txt`; the **About** box reads it).

### Results that survive, and optimisations that settle
- **An analysis or optimisation is found again when you reopen the file.** The result cache was keyed by the memory address of the part's expression, so it was written to disk but
  never found again in the next session (a topology optimisation of 20 minutes solved again every time). The key is now the part's expression with the **numbers of the script's
  `var()`s** in it, which does not change between sessions; a part the library cannot key that way is kept in memory only, never written under a key that could not be found.
  Results solved by an older version are solved once more (the cache version went up).
- **Every menu entry that writes numbers asks first.** Creating a *topology_optimization* from the menus used to write `volume_fraction` and `element_size` you had not seen. Now every
  entry of the creation menus (New 3D shape / 2D shape / point / surface / field, Add operation, Add simulation, conditions and materials) that has to put values into the call
  opens a small form where the mouse is, with **each value, its unit and a hint**, the defaults in place and the first one selected; **Create** (or `Enter`) writes the call, Cancel
  writes nothing, and a value that is not a number turns red. What is shown is what is written (the form is made from the same call the menu would have written). A box, a sphere or a
  union needs nothing asked and is written at once.
- **`topology_optimization`: crisp, settled, and it can grow.** Analysing a bracket test -- material removed on one side that **came back** on the other late in the run -- found no
  bug but three causes: the loads and the part are not symmetric (so the two sides rightly differ), the run was cut at 60 iterations while still changing, and a third of the part was
  grey, so which grey became solid depended on the threshold the end result was cut at. Now:
  - `sharpness=16` (new): the density is pushed towards 0 and 1 (a Heaviside projection that sharpens as the run goes on), so the part ends up solid or empty and nothing wanders in at the end.
    On that bracket the compliance is 21 % lower and the late regrowth is gone. `sharpness=1` gives the old soft density.
  - `iterations=100` (was 60), and the run stops when the design has settled; if it stops at the limit while still improving, the output says so and suggests more.
  - `grow=` (new, mm): the part may also **thicken outwards** by up to that much where that makes it stiffer -- sections that are too thin grow, material that carries nothing goes. The
    design starts as the part, `volume_fraction` is of the part's own volume, and the part is not grown round the supports and loads.
- Result legends, step labels, the one-line summary of each analysis in the output pane and the progress bar's texts (*Optimierung, Iteration 7 von 100*) are in the language of the program (*von Mises stress (MPa)*, *iteration 7 of 100*,
  *load 35 %*, *Statische Analyse: 9,956 Tetraeder …*). The **result card widens** to the longest field name instead of cutting it off.

### Regions, fields and conditions from the viewport
- **Boundary conditions are made for a body and its regions.** Select several models -- the first is the body, the others are where the condition acts -- and
  **New support or load / thermal condition / flow condition** write `fixed(surface_from_bodies(part, base, lug))` (and `force`, `fixed_temperature`,
  `heat_input`, `convection`, `inlet`, `outlet`, `wall`, `slip`; `heat_generation` takes the regions themselves, it is a heat in a volume) instead of spawning a box. A region is a body, any field, or a
  selected surface (which stands for itself). **One model selected, or none: the box at the cursor, as before.**
- **The same for what takes conditions.** `static_boundary_conditions` and the five analyses, with several models selected, use them instead of boxes on the ends of
  the part: regions (the first a support, the others loads; a temperature then heat; inlet, outlet, walls), conditions that are models of the script
  (`supports=[fixed_1], loads=[force_1]`), a set of conditions, a material or a fluid. Without anything usable among them they write the boxes as before.
  The element size of an analysis is a twentieth of the first model's size, not of the last one selected.
- **The render region grows by itself** when a new model comes into the script and reaches out of it (a new object, an array, a moved copy, a typed line):
  `view.set_bounds` is rewritten (or added) with whole numbers and a little room. A region that comes from `roi(...)` stays that, the model given to it as a box. Opening a file
  and models that were there already do not change the region.
- **The corners of the region are whole numbers** in the tree and the status bar (a minimum rounded down, a maximum up; two decimals only under 1), and typing allows two
  decimals. A region that is not set shows the default one, -10 to 10, not zeros.
- **Every condition takes any number of regions as arguments.** `fixed(a, b)`, `force(a, b, (0, -100, 0))`, `fixed_temperature(a, b, 20)`, `heat_input`, `heat_generation`, `convection`,
  `inlet`, `outlet`, `wall` and `slip` / `symmetry` take their regions -- bodies, fields, selected surfaces -- one argument after the other, and make the one region of them themselves
  (a list of them, or `region=[...]`, is the same). The menus write them that way: no `union(...)` made by hand in the script (`fixed(selection_1, surface_from_bodies(part, base))`).
  A body, surface or field **dropped on a condition in the model tree** is one more argument (`fixed(lug)` + extra -> `fixed(lug, extra)`; the box the menu made for it is swapped for
  the model); a body dropped on a set of conditions becomes its part. `lattice_surface_conform(within=[a, b])` takes a list too.
- **A selected surface or field is passed in, not replaced by a box.** Right-click a surface you picked (or select it) and choose **New support or load / thermal condition / flow condition**:
  `fixed_2 = fixed(selection_1)` -- the model itself is the region, the condition acts where the part meets it. The same for one selected field (`fixed(swell)`: where the field is below 0).
  One body, or none, is still the box at the cursor. With a surface picked on a part alone selected, **Simulation -> static_boundary_conditions** writes
  `static_boundary_conditions(bracket_0, supports=[fixed(selection_1)])` -- the part is the one it was picked on.
- **A field selected with a body goes into the number of the call.** *offset, shell, thicken, shell_inside / outside / centered, offset_exact, shell_exact, smooth, round_edges, fillet* and
  *lattice* (its thickness) take a selected field in the place of their number: `offset(plate, swell)`, `lattice(plate, cell_periodic('gyroid'), cell_size=5, thickness=swell)` --
  what dropping the field on the call in the tree does.
- **A force asks what it is.** Choosing a force from a menu -- **New support or load -> force**, or any entry that writes one (`static_boundary_conditions` and the
  analyses with regions) -- opens a menu where the mouse is, of the style of the surface selection's, for the force in newtons, x, y and z, **0, -100, 0** to begin with, with the first
  number selected so that typing replaces it; **Create** writes it into the call, Cancel (or a click elsewhere) writes nothing. **Nothing is asked when the force is given**: with a load
  that is a model among the ones you selected (`force_1`), *static_boundary_conditions* and the analyses use it, and the regions next to it become supports instead of loads with a force of their own.
- **An analysis made from a set of boundary conditions uses it.** With a `static_boundary_conditions(...)` model selected -- alone or with a material -- *static_analysis*,
  *modal_analysis* and *topology_optimization* are written for ITS part with it as the conditions:
  `static_analysis(bracket_0, static_boundary_conditions_1, element_size=2)`. They used to take the set as the part and lay the boxes of the menu's own on it, so a side of
  the part was held by a box and carried no stress, and the surfaces you had selected were not used.
- **Force arrows show on the surfaces you loaded.** A force on the wall of a bore, or on a surface it only grazes, got no arrows and the note "a force acts where the part is not":
  the arrows are shown now, as long as the bore leaves room for them (shorter in a bore), and the note is for a region that really holds no surface.
- **An analysis of a part that is ready to be dragged works.** Selecting a model writes the `expose(...)` and `handles(...)` lines that make it draggable, and every analysis of
  such a part (static, modal, thermal, flow, topology) failed with "the part has no material inside the analysis region": the kernel's evaluators that are made without
  the script's `var()` numbers read every variable as 0, and the part collapsed. The program now tells the kernel the number of each variable as the script makes it
  (`DefaultVars`), and an evaluator that is not given a variable's number takes it from there (a number it is given always wins; the table is cleared when a run begins).
  This is for every kernel function that evaluates a tree of its own, not only the analyses; the oracles of a field read the numbers too.
- **The extent of a model in the scene is right under load.** The quick estimate (80 ms) came back as a coarse box when it was cut short -- 500 mm round a part of 30 mm -- and the
  element size and the boxes a simulation of the menu is written with were sized by it. It is searched again inside a cube round the box it found, and the extent is kept by
  the model's expression and the script's numbers, so a part that is ready to be dragged is searched once.
- **Clicking into any field selects all of it** (the render settings, the resolution of a model, the spin boxes of the menus, name fields), so that typing replaces it.

### Speed and feel
- **Dragging never writes a number into the wrong place (the "unclosed bracket" error).** A drag writes its numbers into the `var(...)`
  calls of the script at the lines and columns that the last finished run recorded -- and the script can be another text by then: selecting a
  model makes FielDes write an `expose(...)` block above its handles line, a click on the tree puts a line in or takes one out, a key is
  typed. A number written at an old place went into the middle of some other line (`var(31.73047.0)`, then `SyntaxError`). Reproduced
  in a window on the previous build by selecting a part and dragging its gizmo at once, then fixed: the places are now cursors of the
  document, which every edit moves along (the tree's, the keyboard's, undo, the drag's own); a run's places are taken over only when the
  run was of the text that is in the editor; and what stands at a place is checked to be the number before it is written over (a number
  that is not there is not written, and the script is run again). Also correct now for a line with an accent before a number (Python
  counts bytes, the editor characters). Checked in a window on a part with handles (select a part and drag at once, lock another and drag, unlock and drag):
  every step parses and the numbers land in the part's handles line.
- **The viewport follows the tree's lines.** The tree moves to the new lines the moment a click edits the script, but the shapes in the viewport
  knew the lines they were made on only from a run -- so for the seconds the run took, selecting a model in the tree lit up another shape (or
  none, and no gizmo to drag), and a click on a shape selected another row. The tree now tells the viewport where each line went (the same
  for drops), and the shapes' lines move with it.
- **A render goes on when you change something that has nothing to do with it.** A model's numbers (`var()`) were told apart by the
  order they appear in the script, so one new `var()` line above a shape renamed every number below it: the shapes were "new" and
  every render started again. The numbers are found again by the statement they are in (`name = function` of its statement, and
  which one of that name it is) and their place in it, so a new line, a comment, a changed number elsewhere or another statement
  leave the shapes (and their renders and meshes) as they are. Measured with a window: three unrelated edits above two shapes,
  2 of 2 shapes kept each time (the old numbering restarted both at the first edit).
- **A run that a newer edit has already replaced is not made.** Every edit of the script used to queue a run of the script as it was
  then, and the runs went one after another, each to its end. Only the last of a row of edits runs now; the one that is running
  is stopped as before.
- **Delete is instant too.** The bin of a row (and `D`) takes the statements out of the tree at once, with the lines under them moved
  up, and what an operation was made of stays as rows of its own (5-7 ms to write and follow, in a window: three deletes and a lock in a row
  while the script was running). The bin of a `custom_resolution` row goes the same way.
- **The model tree follows its buttons at once.** The eye, the lock and the render cache write their lines and the tree changes with
  them, in the same moment (a few milliseconds), without waiting for the script to run: every row is on the line the edit gave it, the
  clicked model has its new state, and the rows are made again from that. Clicks in a row while the script is running are all made
  at once, from the tree as it is now, and make ONE run of the last text -- six clicks in a row on a script that takes four seconds
  to run were done in 0.3 s, and the script ran once. The gizmo button shows its new mode at once as well (its lines are the run's to
  say: a second click on it waits for the run, as before). A tree that is followed this way is checked against the text: if a line
  that no edit touched is not the same afterwards, or the lines do not fit, the tree is not followed, nothing is guessed, and the
  tree waits for the run as it did.
- **The model tree is there when a file is opened.** The tree of every run that worked is kept (by the md5 of the script's text, and of
  the program and library that made it; sixty are kept, the oldest go) in `scene-cache` next to the render cache
  (`FIELDES_SCENE_CACHE_DIR` moves it). Opening the same text shows that tree at once, correct, and editable, while the script runs
  for the first time -- measured with a script that sleeps 8 s: 2.5 s after the start, the first launch has two of three rows, the second
  all of them. The run's scene replaces it when it is done.
- A scene from a run that was begun before an edit of the tree is not shown over the tree that edit gave (it would take the tree back
  to the text before it for a moment).
- **An edit of the tree never lands on the wrong line.** In `keuken_no-drag_handles.py` a lock statement had been commented out as if it
  were a hidden model (`# hidden: keukencombinatie_1 = lock(keukencombinatie_1)`, and a ghost row for it in the tree): a hide made from rows
  that were of another text than the script's wrote on the line they pointed at, which was the lock line. Now every edit that comments out,
  rewrites, deletes or inserts under a line it found by the rows first checks what is on that line -- the line that shows the model is
  `x`, the one that hides it `# hidden: x`, a lock is `x = lock(x)`, and so on for handles, expose, the render cache, the resolution and
  the definition -- and when it is not, **nothing is written**: the line under the tree says why (what the line is, and what the rows
  said), the script is run again to bring the rows up to date, and the note says again when that is done. Covered: the eye, the
  multi-select eye, isolate, lock and unlock, the render cache, the gizmo mode, delete (the bin and `D`), removing handles, the reimport
  of a part, reset of an import, the resolution row and its menu entry. (Drops and renames work out their own edit from the whole text and
  were not changed.) Tested in a window by moving the rows' lines onto the wrong statements: the script came out unchanged
  for the eye, isolate, delete and unlock, and the same click on right rows worked.
- **A comment that begins `# hidden:` is a hidden model only when what follows is a name or an expression.** `# hidden: x = lock(x)`
  (a statement that was commented out) is a comment now, and no row of the tree.
- **The model tree is built about a hundred times faster in a big file.** The rows were made inside the view, which then laid out
  every row it had at each icon and tooltip that was set: 405 rows took 6.2 s (14 ms a row, 94 % of it the making of the row) --
  and the window was frozen for that long at every run and, with the buttons following clicks, at every click. The rows are made
  apart and put into the tree once: the same 405 rows take 66 ms (12 ms for the rows). Clicks on a file with 400 models: 33-59 ms
  to write and follow, 50-70 ms to make the rows.
- **A model that is selected is made ready to be dragged when the script has run**, not while clicks of the tree are being followed:
  the lines it writes (`expose`, `handles`) would have made the rows wrong again, and the next click wait for a whole run.
- A test window of the harness no longer crashes when a scene arrives while it is looking for a row (`treebutton`, `treeclick`).

### One body at its own resolution
- **`custom_resolution(body, resolution)`**: ONE body is meshed at a resolution of its own (samples per mm, whatever
  `view.set_resolution` says), so a fine part does not make the whole scene fine -- or a plain big one is drawn coarser. In the
  model tree it is a **property row under the variable** (type the number: `part = custom_resolution(part, 3)` is written as you type;
  the bin takes the line away) and **Custom resolution** is in the right-click menu of a model (on a row of the tree, a line of
  the script, or in the viewport). It changes how the body is drawn, not what it is. Documented in the reference and in
  [Caching and performance](docs/caching-and-performance.md#resolution-and-quality).
- **It works on a body that reaches out of the scene's region** (found in `still_broken_lattice.py`: the lattice's box is an
  estimate and went 17 mm past the pump's region, so the viewport silently drew it at the scene's 0.71 samples per mm and the
  number in the tree changed nothing). The body is meshed at its own resolution over the part of its cube that is in the region.
- **A number that is too fine is capped, not an error**: 2000 samples along the body's longest side is the finest there is. Typing `8`
  for a 262 mm lattice raised an error, which took the picture and the row away, so the number could not be corrected in the tree.
  The body is drawn at the finest there is (7.63 here) and the row says so in amber (`→ 7.63`, why in its tooltip).
- **A plus button at the very left of the top bar**, before Open: a new script (File → New, `Ctrl+N`). The plus is white, and it leaves the room of the arrow the other two have, so the icons are evenly spaced.
- **The numbers in the model tree's fields are centred** (the region, the resolution, the quality and the resolution of a model), not
  left-bound; a number too long for its field shows from its first digit, not its last.
- **A resolution given to a PART of an import has its row too.** The parts of an import are rows under the import, not model rows, and
  only model rows got the `custom_resolution` row: the line was written (by the right-click menu of the part's row, or by hand) and the
  tree showed nothing. The row is under the part now, the same one (number, bin, the amber cap note). A part that was imported as a
  mesh (tessellated: drawn from its own triangles) says `(a mesh)` in its row, with the reason in the tooltip: a resolution changes
  nothing for it. Tested in a window on a kitchen part (meshed at its own 2 samples per mm, as the render log says) and a pump part.
- **The bin of the row (or deleting the line) takes the resolution away at once**: a shape that had one kept it until FielDes was
  restarted.
- **The default number of the menu entry is the scene's resolution** (it was always 10 -- the scene's was looked up under the wrong
  name), so that nothing changes until you type over it.

### Import
- **A part whose free-form faces are fitted badly is tessellated, not reconstructed.** The reconstruction measures how far each fitted
  B-spline face is from the exact face; `import_model` tessellates the part when the worst is `fit_tolerance` (1 % of the face's
  size) or more -- not every part with B-spline faces, only those that do not import properly (in the kitchen file, two parts off by
  2.2 % became meshes; the others, off by 0.4 % and less, keep their faces that can be dragged). `fit_tolerance=None` keeps every
  reconstruction.
- **The model tree shows how each part was imported**: a cube for a reconstructed part (its faces can be dragged), a triangle cut
  into triangles for a tessellated one (a mesh: only its gizmo moves it); the tooltips say why, and the gizmo button of a mesh says it
  has no faces to drag.
- **No legend and no reading under the cursor for the shading of a fit that is off**: the parts light up from grey to red, and that is
  all.
- **One function imports a model: `import_model(path)`.** A STEP file or a mesh (STL, OBJ, PLY, 3MF, glTF), a list of
  `(shape, bounds)` back, one per part (one for a mesh). For each part of a STEP file it chooses: **tessellated** (its exact surface
  as triangles, made a distance field) when more than a tenth of its surface is free-form (B-spline) — `threshold=0.10`, measured
  on the test files: the parts of the examples have none to a few percent, the sculpted ones more than half — or when it is a
  surface body; **reconstructed** (a formula from its faces) otherwise. `reconstruct(path)` and `tessellate(source)` make every
  part one way, by name. **`tessellate` works on anything**: a STEP file, a mesh file, a list of parts, and ANY field
  (`tessellate(shape)`: its surface meshed, the exact distance to that mesh). `import_step_parts`, `import_step`,
  `import_step_parts_reconstructed`, `import_step_tessellated(_parts)` and `import_mesh` are gone everywhere (library, docs,
  examples, tests, the application); the model tree's row of an import has **Import as** (automatic / reconstruct / tessellate),
  **Import model...** is in the menu of empty space, and a part's tooltip says how it was imported and how free-form it is.
- **Surface models import.** The sports shoe was a `SHELL_BASED_SURFACE_MODEL` of 34 open shells — no solid in it, which is
  what "does not contain a solid" meant. Each shell is a part now: a **solid** if it closes, a **sheet** (the distance to its
  triangles less half a thickness, `thickness=`, 0.4 % of the file by default) if it does not. `reconstruct` says that a surface body
  cannot be rebuilt. A face with no surface to tessellate is a failed part that says so (pump.stp's last one).
- **Tessellated parts are drawn from their own triangles**, not meshed from their field: a wall as thin as you like needs no
  resolution (the thin-part resolution problem), nothing is meshed, a part renders at once. The triangles follow `move`, `rotate`,
  `scale`, `reflect` and `handles()` by a matrix; while a `var()` of the part is being dragged its field is meshed instead, and the
  triangles are back when the script has run. `roi_resolution` leaves such parts out (they ask for no resolution and cost none of
  the vertex budget).
- **The ghosts of a tessellated part are gone** (the kernel's mesh-to-distance): slivers and duplicate sheets of the tessellation
  no longer make a solid that is not there; ghosts 6 -> 0, holes 0 on the test parts.
- A failed part says why when it is used in an operator too (`part + 1`), and a part that is a model of the script can be selected, dragged
  (onto an operation, between models), copied and pasted like any model; a part that is not in the script is added by a
  double-click or its menu.

### Surface selection
- **`surface_from_bodies(body, *others, tolerance=None)`**: the surface of the first body where it meets the bodies that follow (inside
  them, or within `tolerance` of them); one body alone is its whole surface. A surface like `select_surface` makes -- a region for
  `fixed()`/`force()`, a surface for `lattice_surface_conform()` -- made from the fields alone. In the viewport: select several models,
  right-click, **Operation → Surfaces → surface_from_bodies** (one model: its whole surface). Checked on the wall of a hole a bolt fills,
  a face under a block, a gap and `tolerance`, several bodies and a list, a sphere cut by a box, the menu, a lattice laid on the patch,
  and in the window.
- **A selection is drawn exactly, also in the middle of a big flat face.** The viewport showed the lit-up patch by the vertices of the part's
  mesh, and a flat face is a few huge triangles, so a patch lying in the middle of one -- the face of a box inside a sphere -- came out as skewed
  slivers between far-away corners (or not at all). A triangle that can hold the edge of the patch is now cut into four, again and again,
  down to a tenth of a millimetre or so on a part 65 mm across; one wholly in the patch, or too far from it to hold any, is left. The edge is as sharp as
  the mesh is fine, for `select_surface` too (checked: the top face of a plate with a hole is one clean rectangle with a round hole).
- **A selected surface is a surface, with no thickness.** `select_surface(..., thickness=)` and the *Thickness* entry of the
  right-click menu are gone: a surface has none. The selection is of the *Surface* kind in the model tree (the separate
  *Selection* kind and its icon are gone), like any open surface; as a field it keeps the thinnest layer the samples cover,
  which is what makes it a region at all, not a setting (thicken it with `offset_by(selection, mm)` for a coarse analysis).
- **The smooth selection stops where the surface bends tightly.** Smooth mode used to limit the turn from one step of the walk
  to the next -- 30 degrees over 1.7 mm on a shoe, which nothing but a sharp edge ever reaches, so it took the whole outside. It is now a
  limit on how tightly the surface bends: `angle` degrees for every 10 mm of the walk (measured from the sample about 10 mm back).
  On the pump shoe: 15 degrees or more takes the whole outside, 10 about half of it, 5 a patch round the seed. The menu's default is
  10 degrees for both spreads, and the value it remembered from before (a different quantity) is dropped.
- **`select_surface` on a part that was isolated no longer fails.** Isolating a model writes `part = handles(part, move=(var(0)...))`
  into the script; `handles()` lost the part's box when its numbers were `var()`s, so a selection made on it had to search the
  shape's extent, which does not work on a mesh in the application (it did in a headless run): "could not find the extent of the
  shape". `handles()` now carries the box (scaled, rotated, moved by the numbers the `var()`s have).
- **A lattice on a thin shell no longer reaches into the air.** `lattice_surface_conform` (side `inside`, the default) gives each
  layer the depth of the body under it, found by reading the field inwards along the normal; it stepped an eighth of a cell at a
  time, so a wall thinner than that was jumped over and read as no body at all, and the layer got the whole reach (three cells)
  standing out of the surface. On a shoe with a 1.4 mm wall and 20 mm cells that left lattice pieces 15-26 mm above the shoe. The
  field is now read in finer steps first, so the layer is the wall (1.4 mm), and every piece lies within 1.5 mm of the part.
- **`lattice_surface_conform` makes a thin layer by default, and has `cell_thickness` and `stretch_cell`.** The lattice used to be as thick
  as its cells were wide; for riveting and the like it is now a layer `cell_thickness` thick (3 mm by default), measured from the
  surface. `stretch_cell=True` (the default) deforms the cells to fit that thickness: `cell_size` along the surface, `cell_thickness`
  through it. `stretch_cell=False` keeps the cells' own proportions (as deep as they are wide): `cell_thickness` of them stands out of
  the surface and the rest, on the other side of it (the inside of the body), is cut off at the surface -- for strut cells and for a
  periodic surface, outside or inside, on a body, a surface or a selection. The argument `depth` is gone: it is `cell_thickness`
  (`None` = as it was: a body filled as deep as it is, or one cell). The default strut of a thin layer is a quarter of its thickness
  across at most (a thinner one is finer than the viewport can draw).
- **A lattice on a selection is laid out as a surface, by a method of its own.** A selection is a surface: it used to go through the
  closed-body layout (the whole part had to be mapped as one closed surface, and the call was refused for a part that is not closed,
  `the cell map does not cover the whole surface ...`) and then be cut to the patch, which left the tops of cells in the air. The layout is
  now told, not guessed from the field: a body, a surface (cut by its region) or a selection. A selection is laid out over the
  surface the picked patch makes -- the points of the walk with their normals, as a field, `selection.surface` (the height above the
  patch along its normal) -- over the part of it by the patch only, by the sheet method. Nothing of the body behind it is looked at,
  and nothing is cut afterwards. `lattice_surface_conform(selection, ...)` stands on the side the surface faces, one cell deep, unless you
  give `side=` or `depth=` (the default `side` is `'inside'` for a body, `'outside'` for a surface or a selection).
- **`select_surface` is made from the field alone** — its value and gradient, like the conformal lattice: a walk over the surface
  from the seed in steps (eight from every sample, each carried onto the surface along the gradient), kept where the surface
  stays within the angle of the seed's (flat) or of the sample before (smooth). No mesh is made or read: it is the same on a reconstructed part, a
  tessellated one, a mesh, a CSG model, however thin. The selection is the distance to the walked samples (a kd-tree field) less half its
  thickness; `.samples`, `.spacing`, `.cover`, `.patch`, `.whole` (= |field|). `resolution` is steps per mm (default 150 along the
  longest side). Tested on a box, a plate thinner than a step, a sphere, a cylinder, moved shapes, the bracket reconstructed and
  tessellated, and pump_2. The mesh flood (`libfive_mesh_flood`) is removed.

### Model tree and viewport
- **An error leaves no shadow rows.** The `# hidden: name` lines that come after the line that failed (their models were never
  made) were listed as rows of their own, pointing at nothing, next to the real part rows. A run that stopped, or is still running,
  lists only what it got to.
- **The error banner goes when the failing line is rewritten or passed**, not when the whole run ends: the moment the line it
  was on is not in the script any more, or the run has completed a statement after it. A run that takes long (an import, a
  lattice) used to leave the banner up the whole time. If the new text fails again, the banner comes back after the usual moment.
- **Materials are their own kind** (`material`: tan, a hatched block): `Material(...)`, the presets and fluids. They are models in
  the tree, nested under the analysis that uses them, and **dragged onto an analysis** a material becomes its `material=` (a fluid
  its `fluid=`), replacing the one it has. Conditions are dragged the same way: a set (`static_boundary_conditions`) becomes the
  analysis's conditions, a support goes into `supports=[...]`, a load into `loads=[...]`, a thermal or flow condition into the list the
  analysis takes — never a question, and the reason when it cannot be done.
- **The simulation menus list them** (**Add simulation** in the menu of empty space, **Simulation** in a model's menu -- the first
  menu stays short): New material (steel, stainless steel, aluminium, titanium, PLA, PETG, ABS, nylon — written
  out with their numbers — and your own), New fluid (water, air, oil, glycerol), New support or load (fixed, force, gravity,
  thermal expansion), New thermal condition (fixed temperature, heat input, heat generation, convection), New flow condition (inlet,
  outlet, wall, slip); a condition is written with its region as a box model of its own (change it, or drag a selected surface
  onto the condition to replace it), and **Simulation -> static_boundary_conditions** writes the boundary conditions of a model. **Lattices**
  (`lattice`, `lattice_surface_conform`) and **Importing** (`tessellate`) are in the Operation menu.
- **Ctrl+D duplicates, Ctrl+C / Ctrl+V copy and paste models** (viewport or model tree focused; Ctrl+D is the editor's multi-cursor
  with the editor focused): the model's statements with free names, copies referring to each other's copies, one undo step.
- **The part rows have an eye each**; only the **assembly** (the import row the parts are nested under) has none, since it is the
  file and not something drawn. Imported parts are ordinary models: nothing is locked unless you lock it (the lock button, `R`).
  A locked model that is selected says why it has no gizmo (a line under the tree), and unlocking one that was never made ready
  makes it ready (its gizmo line, its draggable surfaces).
- **A part shown with `I` (or `V`) is made ready to be dragged.** A part that was hidden and was shown by isolating it got its gizmo
  (from the `handles(...)` line it had) but never the numbers that place its surfaces, so its faces could not be pulled; only the
  eye prepared a model. What is selected is made ready after every run now (nothing is written for a model that is ready).
- **An orange dot marks every model whose surfaces cannot be pulled.** Selecting a model writes the numbers that place its surfaces
  (`expose(...)`) when they are no more than 120 -- a bigger one would be pages of script -- and the model tree now shows which
  models have no such handles from the start: a small orange dot right after the name (parts of an import included: a tessellated
  part is a mesh with no faces to pull, a reconstructed one with more than 120 numbers, such as the kitchen file's part 3 with 1620,
  is not made draggable). Its tooltip says which and why; the gizmo moves such a model all the same, and selecting it says it under the tree.
- **The disaster file crashed while the mouse moved over it.** Hovering a model asks which numbers it is made of, and the kernel's
  `Tree::walk()` of a tree that has been moved (remapped: a gizmo, `handles()`) returned pointers into a flattened copy that was
  already gone, so the walk read freed memory: heap corruption or a jump to a freed object, a few seconds after opening, whenever a
  moved model was under the cursor (exit code 0xC0000374 or 0xC0000005; reproduced by moving the mouse over the file while it
  loads). Every walk of such a tree keeps the flattened copy alive now (the hover, the field walker, the save check, the axes, the
  serializer). The hover also checks the shape number it reads from the picking picture, as every other place did.
- **A crash leaves a report**: `%LOCALAPPDATA%\FielDes\FielDes\crash\crash-<date>.txt` with the exception and the places the
  program was at.
- **A body with no surface is not a failure.** `pump.stp` lists a last body that has no surface to import: its row is dashed and dimmed
  with a tooltip, not red and struck through.
- **A locked model can be in a selection of several again** -- to select a handful of parts and unlock them at once (`R`, or the
  lock button of any of the selected rows, which then acts on all of them). A selection with a locked model in it cannot be
  moved or edited (no shared gizmo, nothing written for it), and a popup says so: when a locked object is part of a multi
  select, the multi selection can no longer be moved or edited. A line under the tree says how many are locked.
- **A click on the tree waits for a script that fits.** Eye, lock, gizmo mode, cache, delete and the import buttons make their edit
  from the lines the last run reported; clicked again before that run was done (a big file runs for seconds) the second edit hit
  the lines of the first one's text, and a definition was commented out or deleted -- the script then ended in an error. The
  scene says which text it is of, and an edit made while the text is another waits for the scene that fits (one at a time; a script
  with an error refuses it and says why).
- **The model tree no longer reloads when you click an eye or a lock.** The script runs again after such a click, and once a
  statement (an import of ninety parts) had run for a moment the tree was replaced by the rows of the statements that were
  done -- one row -- and filled up again when the run ended. A tree that is shown now stays as it is until the run is done (only
  an empty tree, a script just opened, fills row by row as the run goes on), and the button that was clicked changes at once.
- **Dragging numbers no longer eats parentheses.** A drag writes the numbers of a line of `var(...)`s into the script as they
  change; the place of the numbers to the right of one that changed was moved along only when they changed in the same step,
  so one that changed in a later step was written one character off (`var5.200222)`).
  All of them move along now.
- **`I` keeps the tree where it was**; **Fix All** puts
  fixes after `from fieldes import *`; a running render keeps going when something else changes (only a shape whose geometry
  changes starts again).
- **An error shows**: after 1.3 s of a script that does not work, the model tree header says "error" and the viewport gets a red frame
  and a banner with the line (click to go there); it clears when the script runs.

### Fields
- **The field viewer is not kept anywhere.** The disc could not be dragged out of the render region (a box round the part, in a
  big file a small one); nothing limits it now, and the Position slider, which spanned that region, is gone: the arrows move the
  disc.
- **A field has "Body from field" at the top of its menu** (its row in the tree, and the field viewer's disc), next to
  Operation > Field math, which has it too.
- **The field viewer follows the hand.** The disc was sampled again only when the mouse rested: every move started its timer
  anew and a pass on its way was abandoned at every tick, so a drag showed one picture, at its end (measured: 1 per drag of
  40 moves; now 20). The same for the section plane. The numbers of a field that a gizmo drags reach the viewer as they move.
- **The analysis fields read the script's `var()` numbers**: `gradient_magnitude`, `normal_field`, `wall_thickness`, `curvature_field` of a shape moved by `handles()` (or any shape with a `var()`) used 0 for every variable (the kernel printed "uninitialized variable" hundreds of times); they are given the numbers the evaluator holds now, and follow a drag.
- **`body_from_field(field, level=0)`**: a body where the field is below `level` (the inverse of `field_from_body`).
- **`low_level_field(logic)` and `low_level_body(logic)`**: your function of the coordinates, called once with x, y and z as fields
  (or x and y; or x), is the field or the body — `maximum(...)` and `minimum(...)` stand for Python's max and min. Right-click ->
  New field -> low_level_field (or New 3D shape -> low_level_body) writes the function, with a first working logic at the cursor,
  and the call. Example 21 uses them.

## Next — fields everywhere, custom blocks, a typed and nested model tree

### Fields everywhere
- **Wherever a number goes, a field goes.** A size, a radius, a thickness, a spacing, a blend, an offset, a scale ... of every
  function of the library can be a field instead of a number (`offset(part, ramp(z_field(), (0, 40), (0.2, 2.0)))`), and is
  evaluated where it is needed, exactly: nothing is sampled onto a grid. A constant field was run
  through 98 numeric slots and gives what the number gives. Added or fixed: `ramp` / `normalize` ranges, `attractor`
  (the radius; a point or points as the centre), `smooth_union` / `smooth_intersection` / `smooth_difference`, `union_all`,
  `repeat`, `noise_field`, `distance_to_point` / `_plane`, `radial_field`, `angle_field`, `polar_field` (centres; a point works),
  `mass_properties` (a density field), `offset_exact`, `shell_exact`, `round_edges`, `fillet`, `smooth` (the radius),
  `ellipsoid`, `elliptic_cylinder`, and in lattices `skin`, `blend`, `skin_blend` and the struts' blend. What cannot be a field
  (counts, tolerances, resolutions, a Poisson's ratio) says so when given one.
- **Graded lattices.** `lattice(body, cell, cell_size=<field>)`: big cells here, small cells there, by blending lattices a factor
  of two apart (up to a factor of 16), for every periodic cell including `cell_custom`.
- **Fields in the analyses** (tetrahedral elements): `Material(E=field, density=field, conductivity=field, expansion=field)`, a
  load's `profile=field` (the total stays; it is spread over the surface in proportion to the profile), `fixed_temperature`
  with a field, `convection` with a field for the coefficient and the ambient temperature, `heat_input` / `heat_generation`
  with a profile. Checked against beam theory and Fourier's law; topology optimization and
  the modal analysis take them too. The flow solver takes numbers and says so. The kernel evaluates each field at the centre
  of every element (`TetProblem::setStiffnessField`, `TetThermalProblem::setConductivityField`, ...); the result cache keys
  include them.
- **Points and surfaces are models.** `point(x, y, z)` is drawn as a small ball, has a gizmo, and is accepted wherever a
  coordinate goes (`distance_to_point(p)`); `plane`, `sphere_surface`, `cylinder_surface`, `wave_surface` are surfaces drawn as
  thin sheets. A 2D shape is drawn flat. A **field is not drawn**: selecting it in the model tree opens the section viewer on it,
  which paints a plane through the render region with the field's value at every point (the colour scale found once on a coarse 3D
  grid; hover reads the value; move the plane to see it in 3D). A field model has no eye in the tree.
- **Arithmetic on fields.** Field * field, field / field, field ** field and `2 ** field` work like number * number, pointwise
  (`__rpow__` was missing). The same as functions that always make a field, also from bodies, for the tree and its menus:
  `add_fields`, `subtract_fields`, `multiply_fields`, `divide_fields`, `power_field`, `min_fields`, `max_fields`, `abs_field`,
  `negate_field`, `sqrt_field`, `square_field` (right-click: Operation, Field math).
- **`field_from_body(body)`**: the values of a body as a field -- not a body, not drawn, free to be multiplied and still valid.
  (A body itself keeps its true scale.)
- **A point goes wherever a position goes**: `Point` reads as its three coordinates (`move(part, p)`, `sphere(5, p)`,
  `distance_to_line(p, d)`). In the model tree a point can be dropped on an operation: it takes the place of a position written
  as a tuple (`distance_to_point((5, 5, 5))` -> `distance_to_point(anchor)`; a flat shape's centre becomes `(anchor.x, anchor.y)`),
  never of a size, a direction or a scale; and a dropped field never takes the place of a count (`array_x(body, 3, 12)` takes the
  12's place, not the 3's), with the reason given when there is nothing it can take.
- **Ctrl+drag makes a shadow, and the model stays.** A model that **nothing else uses yet** stays a top-level
  row when it is Ctrl-dragged into a call, and the call shows a shadow of
  it: the drop writes a `# shadow: name` comment on the call's statement, which says the call holds a reference and is not the
  model's first user. A plain drag onto that call removes the comment and moves the row in; deleting the reference removes it too.
  A shadow never stands by itself at the top level: dragging such a statement **above** the model's row (which used to do nothing),
  or the model's row **below** the statement (which used to be refused), asks once -- *the reference inside the statement becomes
  the real model*: its definition goes directly above the statement, the comment goes, and the separate top-level row is gone.
- **Recent files**: a small arrow beside the Open and the Import icons lists the scripts opened and the models imported lately.
- **Open example file** is its own entry of the File menu (the file dialog opens in the examples folder next to the program), and the
  guided tour ends by pointing at it.
- **The field viewer is a card of its own**, no longer the section card in a different mode: a coloured **disc** (radius slider)
  that cuts nothing, with the section card's arrow along the way it faces, two arrows in its plane and the dot in the middle for
  moving it freely. It starts where the field is about (the point or body it was made from, found by Python: `_field_origin`,
  inherited by fields made of fields), has no close button (it closes when no field is selected) and, with several fields
  selected, a menu chooses the one shown. The section card steps aside while it is open and comes back as it was.
- **Cards are draggable and resizable**: the model tree, the section card, the field viewer and the result card can be dragged
  anywhere in the viewport (by the header or any empty place) and resized from their edges and corners; they keep their place
  relative to the window and are remembered between sessions (`CardController`).
- **The guided tour has a Ctrl+drag step**: "A reference with Ctrl" adds a sphere and a union and shows a Ctrl+drag making a shadow
  reference (the pretend hand shows a Ctrl key cap).

### Custom blocks
- **Your own functions, in every script.** Every public function of a `.py` file in the blocks folder is a block: no import, no
  registration. They are written with the whole library at hand, may use the blocks of the files before them, and appear with
  call tips, completion, go to definition and in the right-click menus (*Add operation → Custom blocks*, *New custom block*).
  **Settings → Blocks folder…** chooses the folder; it is watched, and a script that uses a block runs again when its file is
  saved. A block file's errors are named in the output. See [Custom blocks](docs/blocks.md); `blocks/sample_blocks.py` has four.

### The model tree
- **The gizmo shows at once.** Selecting a model used to show its gizmo only after the script had run with the new `handles(...)` line
  (a moment, longer for a heavy script). It is drawn immediately now, from where the model is (the same point the real one has), a
  little transparent; the real one takes its place when it is ready. A press on the early gizmo is kept: the drag starts as soon as
  the real one exists, if the button is still down.
- **Types with icons.** Every kind of thing — 3D shape, 2D shape, field, surface, point, simulation, conditions, lattice cell,
  selection, import — has an icon of its own colour and shape in the tree and in the menus that make it; a model made by a custom
  block carries an *f* badge.
- **Nesting.** The models an operation is made of are its children (a model goes under the first statement that uses it; the
  others show it as a dim *uses x* row).
- **Drag and drop.** Drop a row on an operation to make it one of its inputs (replace one, or add one to `union` and the like);
  drop it between rows to move it in the script, with what it is made of; each is one undoable edit, refused with a reason when
  something would be used before it is defined.
- **A field dropped on an operation takes the place of a number**: drag `swell` onto `thick = offset(plate, 1.0)` and the call
  becomes `offset(plate, swell)` (the first plain number of the call). A model dropped on an operation is added to it, or takes
  the place of the only model it works on; no menu is asked.
- **The right-click menu in empty space** makes things by kind: *New 3D shape / 2D shape / point / surface / field / custom
  block*, then *Add operation*.
- Selecting a model no longer writes gizmo lines for models that are not shown, and a model that is made of the one that gets
  numbers loses its now stale `expose()` line instead of failing.

### The guided tour
- **A tour on the program itself.** The first time FielDes starts it offers a guided tour (**Help → Guided tour** after
  that): short cards over the live window. Each step dims everything but what it is about, points at it with an arrow and a
  glowing frame, and lets the user do only that one thing (the rest of the window and the keyboard are held back so that
  nothing confuses); **Show me** does it with a pretend cursor, through the same mouse events a hand makes (typing a number
  into the script, dragging a surface, clicking a row, the right-click menu, dragging a field onto an operation, renaming,
  editing a render setting). Every step can be skipped, and so can the whole tour. The script is shown as half of what the
  step is about: the lines a step concerns stay lit and framed in the editor, and every line that changes or moves -- typed,
  dragged in the viewport, dropped or renamed in the tree -- glows for a moment (a line that only moved down because another was
  put above it does not). It loads a model of its own (a plate with a
  hole), so it never touches the user's script.

### The model tree
- **Rows that are already nested can be dragged and dropped** like the others, and the tree answers **at once**: it is
  updated from the edit before the script has run again (the run confirms it). Dragging no longer writes gizmo lines into
  the script while the mouse is down, which is what made a drop wait for a run.
- **Rename by double-clicking** a variable's name (`F2` too): every use changes, scope-aware (strings, keyword arguments,
  attributes and a function's own variables of the same name are left alone).
- **Render settings are editable** in the tree: the region, the resolution and the quality are number fields.
- **The tree is the structure of the calls.** Nesting, renesting and denesting always edit the arguments of a call: a model
  dropped on an operation becomes an argument, dragged out of one it leaves the arguments, dropped between the children of
  an operation it stands at that place among them (and the children are listed in the order of the arguments). A model used
  by several statements has its row under the first and a **shadow** row (half transparent) under each of the others; a
  shadow can be dragged (only its reference moves), copied, and deleted (button, `D`). **Ctrl+drag moves nothing**: the model
  keeps its row, and a shadow appears in the call it is dropped in. **A shadow cannot go above its original** (the first
  statement that uses the model): a message explains why and offers the easy fix, which is its default -- the first use becomes the
  original place and a reference stays at the old one -- with *Do not show again* (always do that). It is asked once, from the tree
  the edit would give, so it also catches a statement with shadows dragged above the original, or the original dragged below it,
  and lists all the shadows in one message. Ctrl+pressing a row that is
  already selected no longer takes it out of the selection before the drag can begin. **No menu is ever asked on a drop**: a model dropped on an operation is added to it (`union`, ...), or
  takes the place of the only model the operation works on; a field takes the first plain number of the call; anything that
  cannot be done is refused, and a red note at the mouse says **why**. Taking a model out of an operation that cannot do without it deletes the
  operation (and, in turn, what is made of it), after a question whose default is *Delete* (it can be hidden); one undo.
- **`#SECTION` comments fold** the lines up to the next one (an arrow in the gutter, a box that says how many lines are
  hidden), are set apart in blue, and complete from `#sec`; an error inside a folded section opens it.

### Linux
- `scripts/build-linux.sh [--package]` builds FielDes on Linux (Fedora and Ubuntu package lists in the script; made and
  tested under WSL2 + WSLg) and packs a portable folder (`dist/FielDes-linux-x64.tar.gz`, started with `run.sh`).

### Viewers, typing, the tour (round 6)
- **The section view and the field viewer are separate.** The field viewer used to take the section's plane for itself and hide the
  section card while a field was selected. Now each has its own plane in the viewport, its own samples and its own card; the section
  view is switched on by the user (`Ctrl+Shift+X`) and works the same whether or not a field is selected, and the two cards stack
  (the field viewer's below the section card). The disc keeps its three arrows and its dot.
- **Renaming and the render settings write the script as you type.** A name typed in the tree renames the variable in the script
  with every key (a name that cannot be yet waits; `Enter` keeps it, `Esc` gives the old one back, a name that ends up not being one
  gives the old one back too); the fields of the render settings write `view.set_*` with every key, and one number typed is one
  step of undo.
- **The guided tour's "Show me" shows it once and puts everything back.** The script, the selection and the section view are
  restored to how they were when the button was pressed, and the step is not counted as done, so the user does it himself.
  **Every step has a state of its own**: the first time it is what the steps before left, and whenever it is opened again (Back
  included) it is put back as it was, ready for what it asks. A new step, **Cut a section**, shows the section view, and the
  last step draws the File menu with **Open example file** framed, as the blocks step draws its menu.
- **The region has no bin** in the render settings (the resolution and the quality keep theirs: deleting them goes back to the default).
- **Low-level fields in Python**: `examples/21_low_level.py` does what libfive's Scheme `define-shape` / `remap-shape` do -- a cube from
  six plane distances, a twist by `.remap`, a ball and a torus from their formulas joined by a smooth minimum written in the script, a
  gyroid -- with `Shape.X()/Y()/Z()`, the arithmetic and the methods of `Shape`; [Scripting](docs/scripting.md#low-level-your-own-shapes-from-x-y-and-z)
  explains it.

### Documentation
- A [tutorial](docs/tutorial.md) (the written version of the tour), a revised README, new screenshots, and the
  documents of the new features; examples 18 (fields everywhere), 19 (a graded material and a load profile) and 20 (custom
  blocks), none of which needs a STEP file.
- **One sample part comes with FielDes**: `examples/step/PivotBearingSupportBracket.STEP` is in the repository and in the
  portable packages, so examples 01, 05, 08, 10 and 15 run as soon as FielDes is installed. It is a GrabCAD download and is
  distributed at the maintainer's decision, its author's terms being unknown ([NOTICE.md](NOTICE.md) says so, and that it is
  taken out on request). The other sample parts (examples 02, 03, 04, 06, 07, 09, 11, 12, 13) are still not distributed.

## 0.1.0 — first beta

The first release under the name **FielDes**. It is what libfive Studio became on Windows: a design and
analysis workbench driven by fields, now with its own name, design and documentation.

### Application
- **The model tree fills in while the script runs.** A statement that takes a while (an import, a smoothing, an analysis)
  no longer leaves the tree as it was until the whole script is done: once a statement has been running for a moment, the
  tree lists the variables of the statements that are finished, as soon as they are, and the finished script's tree
  replaces it. A script that stops with an error leaves the variables made before it in the tree too. (The viewport still
  shows the shapes when the script has run.)
- **Opening a file clears what the old one made.** The viewport and the model tree were left showing the previous script until the new one had
  run (and for good when it failed), which looked as if opening it had done nothing. Opening a file, a new script or the
  tutorial now empties both at once; what a run of the old script delivers late is thrown away.
- **The sphere a new file starts with works with the handles at once**, and is moved by the shared gizmo of a
  multi-selection: it is named and its numbers are exposed, as a primitive from the right-click menu is. An expression
  displayed on its own (`sphere(3)`) that is selected together with other models is named first (`sphere_1 = sphere(3)`),
  because the shared gizmo moves variables.
- **Several selected models are in a multi-select state of their own and are moved by one gizmo.** Two or more selected
  models are moved by one gizmo, at the middle of them, with its arrows and its centre dot, at once -- whatever way each is
  edited when selected alone, which is kept and is there again as soon as the selection is one model (a model without
  numbers to move it by gets a gizmo line in the mode it is in). Nothing is pulled by its surface meanwhile. `E`
  changes each model's own gizmo mode (what the tree's buttons show); the viewport shows the shared gizmo (unless every
  selected model is set to *never*).
  The **dot in the middle of every gizmo** drags the model (or all of them) freely, in the plane facing the camera.
  Turning and scaling stay one model at a time.
- **Locked models cannot be multi selected.** Adding one to a selection (Ctrl+click, a Shift range, a rectangle) leaves it
  out and says so, with a *do not show this again* box (Settings → Show hidden messages again). A locked model alone can be
  selected, which is how it is unlocked; `R` on several models locks them and they leave the selection.
- **More keys, and one rule for every toggle.** `V` shows / hides and `C` turns the render cache on / off, beside `E`
  (the gizmo mode), `R` (lock) and `I` (isolate). All of them work on all selected models: if the models are not all
  in the *on* state (shown, locked, cached) they are all put there first, and only when they all are in it are
  they all turned off, so a mixed selection is not flipped model by model. Isolate shows the selected models and hides
  every other one.
- **`D` deletes the selected models** from the script (definition, show / hide, handles, lock and cache lines), one
  undoable step for all of them, with one question if the rest of the script still uses their names. The model's
  right-click menu has **Delete** too.
- **A click on empty space deselects everything** (not with Shift or Ctrl held).
- **Dragging a surface is always there; the gizmo has a mode of its own.** The gizmo/handles toggle is gone. A model's
  surfaces can be dragged whatever else is set, and the gizmo -- which has priority where it is shown -- has three modes,
  set by the tree's gizmo button and the `E` key, which goes round them: **click** (the default: the gizmo shows while the
  model is selected), **never** and **always**. `handles(x, ..., mode='click' | 'never' | 'always')`; `'gizmo'` and
  `'handles'` are refused, and `G` and `H` are gone. Selecting a model makes it ready: it gets the numbers its gizmo moves
  it by (a `handles(...)` line) and, if it has none and they are few (up to 120), the numbers that place its surfaces
  (`expose(...)`); a bigger shape has **Make its surfaces draggable** in its menu. An excluded shape cannot be dragged by
  its surfaces. The shared gizmo of a multi-selection shows unless every selected model is *never*.
- **Selecting several models.** Ctrl+click adds a row of the model tree or a model in the viewport to the selection (or
  takes it out), Shift+click in the tree selects the rows from the last one clicked to this one, as in a file list, and
  Left-dragging in the viewport draws a selection rectangle that selects every model lying wholly inside it (with Ctrl held:
  added to the selection); the view is turned with Shift+left-drag or the middle button instead, and right-drag still pans. The selected models are lit up in the viewport; the order they were selected in is kept.
  `I`, `E`, `R`, `V`, `C` and `D` work on all of them.
- **Operations on a selection.** With several models selected, *union*, *difference* and *intersection* in the right-click
  menu are no longer greyed out: *union* unites all the selected models, *difference* subtracts every one after the first
  selected from the first, *intersection* keeps what they share, written as one call under the model defined last.
- **Selecting in the viewport leaves the keyboard in the viewport.** A click on a model no longer moves the keyboard to
  the code editor (it did after a click on a draggable model, and when another editor tab was showing), so the single keys
  that work on the selection are not typed into the script; a click in the viewport takes the keyboard from the editor.
- **Lock is a switch of its own.** A lock button (key `R`) locks or unlocks, by writing and deleting the line
  `part = lock(part)` under the definition, and the gizmo mode the part had is still there when it is unlocked. The `L` key and the `M` (next mode) key are gone, and so is
  `handles(mode='lock')` (and `show=`): lock with `lock()`.
- **Isolate** (`I`, with the viewport or the model tree focused): only the selected model is shown, every other model
  hidden; `I` again shows what was shown before. It is made of the same script edits as the tree's eyes (undoable),
  and the tree says which model is isolated.
- **A surface selection is shown as the patch lit up on the model**: the vertices of the model's surface mesh that
  belong to the patch are coloured and all the others are not drawn (the field itself, a thin layer across the patch, came
  out as disconnected fragments at the viewport's resolution). The new selection is also the selected model in the tree.
- **Right-click menus in the viewport.** On empty space: *New primitive* (every primitive of the library, placed where the
  cursor is: the point of the ray under it closest to the origin, about a hundred pixels across) and *Add operation*
  (offsets and walls, smoothing and rounding, moving, repeating, combining); hovering shows the functions, choosing one
  writes the call into the script with a line showing it and selects the new model. On a model: *Operation* (the same
  list, with that model passed in) and *Select Surface* (the menu that used to open directly). The list the menus offer is
  one file (`fieldes/menu_catalog.py`) and a test runs every entry.
- **Results are shown by stating them**, as shapes are: `result` on a line of its own displays the part coloured
  by the result (the stress, the temperature, the speed, the density, a mode), with the result card. `show()` is gone.
- **The result card steps through every result** -- a slider with play / pause, the same for all analyses: a
  static result from 5 % to 100 % of the load, a mode through a cycle of its vibration, a flow in time through its
  stored times, a topology optimisation through its iterations (the part as it was after each one).
- **Flow view**: a fluid result is drawn like every other result, with streamlines from the inlets, coloured by the
  speed, and particles moving along them drawn over it (the **Flow** button of the result card hides them); a steady
  flow's steps are its solver iterations, the flow developing from the Stokes start.
- Fixed in the flow solver: the triangles of the rounded edge between two slip planes were adopted by their region
  by comparing with the region's mean normal, which is zero for a region of two opposite planes (both faces of a
  slab, both its sides): one side's edge was then a wall at rest along its whole length and that side dragged (the
  speed along it 15 % low). They are now compared with the region's own triangles at their corners.
- The flow solver's incomplete-LU preconditioner is built and applied in blocks, one per core with a little
  overlap (additive Schwarz): the linear solves, most of a flow's time, now run on every core.
- The result card's play has a mode (round and round, back and forth, once) and a speed (a drop-down, ×⅛ to ×4),
  with one-step back and forward buttons beside it; the step slider has a row of its own, the card's whole width.
- **Every analysis is kept across sessions.** Static, modal, thermal and flow analyses and every optimisation are
  written to the result cache (the mesh, every field, the modes, the iterations, a flow's steps) and read back when
  the same problem is asked again in a later session -- nothing is meshed or solved again, as a field is not built
  again by the field cache. A result read back keeps the keys of its fields, so its renders are found again too.
  `[result cache] read ... back` in the log.
- A result's colours are drawn as finely as the solver's elements: the render's triangles are split (red-green, no
  seams) until no edge is longer than the element size, so a field across a flat face is no longer smeared between
  a few corners.
- New name, icon and look: a light interface around the dark viewport, a dark top dock in the style of the viewport cards, with Open and Import icons at
  the left and a Settings menu (keyboard shortcuts, text size, navigation), a slim editor gutter, a compact model
  tree (300 px), small output text, designed for maximised windows on large monitors.
- The window always starts maximized.
- The meshing options are in the View menu; the Debug menu is gone. Text size is `Ctrl++` / `Ctrl+-`.
- Shorter texts: the result card (a short note, details in a tooltip), the section card, tooltips, the guide and
  the messages in the output pane.
- The shape reference opens at a sensible size.
- Model tree: the region-of-interest button is gone; shorter tooltips; per-part Reimport; handles button
  (gizmo or handles) and lock button, also on displayed expressions: a primitive such as `sphere(3)` is given a name
  and its surfaces become draggable.
- Section card: **Whole elements** appears only when the plane cuts a part shown with analysis elements.
- Legends: closing a legend stops probing.
- Scripts run in their own folder, so relative paths work.
- The guide (`Shift+F1`) rewritten for FielDes.
- Editor: **go to definition across files** (library functions, methods, modules next to the script) opens the
  file in a **tab**. The first tab is the script that is rendered (marked with ▶ and bold); the others are for
  editing only, saved with `Ctrl+S`; a module of your own that is saved runs the script again. Completion
  writes the brackets of a function and puts the cursor inside them.
- The key shortcuts `E` (the gizmo mode) and `R` (lock / unlock) work on the selected models; the gizmo sits at the
  model's centre also for shapes made with `var()` numbers.
- The dark parts of the window and the viewport are a little lighter, so the editor is less of an insert.
- Only Python is supported as the scripting language (the Scheme/Guile binding and the other language
  bindings of libfive are removed).
- **Y points up** in the viewport by default (Settings → Rotation mode switches to Z up).
- **Delete button on every row of the model tree** (a bin; the old reset button on imports is a turn-back arrow
  now): removes the object's statement, the line that shows it, hides it and its `handles()` / `expose()` lines,
  after asking when the rest of the script still uses the name; one undo brings it back.
- **Right-click a model: select a surface.** A menu with the variables of a flood fill (flat or smooth spread, angle,
  thickness, radius) writes `selection_1 = select_surface(part, seed=(x, y, z), angle=10)` and a line showing it into
  the script, under the model's definition. The patch is lit up in the viewport.
- **Boundary conditions are drawn.** `static_boundary_conditions(part, supports, loads)` is a shape in the model tree
  (eye, delete) that colours the fixed, sliding and loaded places of the part and draws the symbols of structural
  analysis programs over it -- an array of arrows over a loaded face with the total force beside it, an array of flat pads over a
  support with its name, one arrow for gravity with its value -- by the viewport, so that they are never clipped by the render
  region or ragged at a coarse resolution, and has its own legend. It is the **only** way to give supports and loads: `static_analysis(part, conditions, ...)`,
  `modal_analysis(part, conditions, ...)` and `topology_optimization(part, conditions, ...)` take it as the second
  argument, and the `supports=` / `loads=` arguments (and the lists of `fixed()` and `force()` in their place) are
  gone — the old calls stop with an error that says what to write.

### STEP import
- Solids are rebuilt as fields from their faces: planes, cylinders, cones, spheres and tori exactly; B-spline
  faces by fitted closed-form surfaces, with the fit deviation reported and painted on the model.
- Assemblies arrive assembled, each part in its own units, each occurrence named.
- **`exclude(shape, region, ...)` locks a region of any shape or field against everything done to it afterwards.** It
  cuts the shape into two fields that are always united again: the *free field* (the shape outside the region), which
  every later operation reshapes, and the *locked field* (the shape inside it), which none of them touches. Offsets,
  shells, smoothing, lattices, unions, cuts and the like work on the whole shape and then put the locked field back;
  moving, turning, scaling and mirroring carry both along; copying (`array_*`, `symmetric_*`, `repeat`, `mirror_*`) and
  deforming (`twist_z`, `bend_z`, `taper_*`, `attract_*`, `repel_*`, `twirl_*`) would tear them apart and raise an
  `ExcludedError` -- do them before `exclude()`. For a part imported from a STEP file the locked field is **the part's own
  exact surface, meshed directly from the STEP file and made a field** (the mesh importer's distance field), instead of
  the import's fit: no more mesh union at render time, so the exact-region code of the viewport and the kernel is gone
  (`exact_region_mesh()` too: the field is the mesh). The region is any shape; several regions given at once are their
  union; without a region it is `poor_fit_region(part)`, the places where the fit is poor, made from the fit marker.
  Given the whole import it works on every part the region reaches; given one part (or one entry) on that part only.
  `exclude(body, other, ...)` is also in the right-click menu (Combining), with the shape selected first and the regions
  after it. An excluded shape cannot be dragged by its surfaces. `topology_optimization`,
  `thermal_topology_optimization` and `flow_topology_optimization` read the exclusions a shape carries as regions to
  **keep** by themselves, however many operations came in between; example 08 excludes the lug holes with it.
  **Free-form (B-spline) faces now mesh correctly**: per-face (u, v) triangulation with shared edges
  (watertight), exact spline evaluation, adaptive edge sampling, tilt- and Delaunay-aware edge flips and
  refinement, verified against OpenCascade on the test set.
- Free-form faces that close on themselves (a tube, a curved rail) mesh correctly also in mirrored parts:
  the end of an edge that ran past its vertex, and a closing triangle, made the refinement of the face
  look folded and it was discarded.
- `auto_exclude=True` on `import_step_parts` (opt in): excludes every place where the fit deviates more than
  `exclude_threshold` percent of the face's size, with a field region, not boxes.
- Per-part render resolution (`roi_resolution`), import cache keyed to the algorithm's version.
- **An imported part is one continuous solid.** The cells it is rebuilt from used to touch inside the material
  with a field of exactly 0 on the wall between them, so an inward offset, a shell, a skin or a lattice (the
  field-driven design example) showed internal walls, and a mesh exported from such a part had zeros where a
  re-import has to tell inside from outside. The cells are now bridged by their consensus (see the
  architecture notes); the surface is unchanged, handles still drag the surfaces and the part stays solid. The
  field **outside** the part is exactly what it was (verified against the unbridged field on every example part), so
  the section's field view, outward shells, thickening and offsets read the same. Parts imported earlier are
  imported again once (the import cache is versioned).
- **Mesh import reads back what was exported.** Where the nearest feature of a mesh has no clear side (a sliver
  folded back over its neighbour, a knife edge: a dual-contouring mesh has them along sharp edges), the sign of the
  distance came out wrong, so points a few millimetres from a part exported with `save_stl` read as inside it on
  re-import, and an offset of the imported mesh broke. The generalised winding number now decides there.
- `expose()` with plain numbers no longer reads freed memory, and a plane through the origin that several
  unions share gets one number, not one per union.
- **Offsets, shells and thickening of an imported part use its exact distance.** The field of an imported part is
  exact in sign, but the value outside it is only the distance to the infinite surfaces its faces lie on (and
  inside, the depth is as inaccurate), so `thicken`, `shell_inside`, `shell_outside`, `offset_by` and `offset` of a
  part grew fins, slabs, notches and stray walls that have nothing to do with the operation (on the bracket of the
  examples 11.8 % of the vertices of a 5 mm thickening were off by more than half a millimetre, up to 10 mm).
  These functions now take the part's exact distance (the mesh of the part and the distance to it, cached: made
  once per part, 0.3 s for the bracket) where they are given an imported part: 0 % misplaced vertices. The plain
  field of the part, and with it its display, its section and its analyses, is unchanged. The exact
  distance follows `handles()` (a gizmo, with its var() numbers) and `move`, `rotate`, `scale` and `reflect` in the
  script. A part whose faces are dragged (`expose()`) has the exact distance of its numbers when the script was
  run (the part as edited, when the script is run with other numbers), and while a face is being dragged the
  change of the part's own field on top of it, so a thickened part follows the drag and is exact again at the
  next run. `FIELDES_NO_EXACT_OFFSETS=1` uses the part's own field. Other shapes (a union of parts, a lattice)
  keep their own field; `exact_distance()` is what to use on those.
- **Meshing a field that holds a mesh's distance is 2.5 to 3.5 times faster** (an imported STL, and the exact
  distance above): the dual contouring's search for the surface along an edge took 64 points per edge, each of them
  a search of the mesh's triangles, where a bisection of 10 points finds the same crossing (to a 1024th of the
  cell, past what a float holds at the coordinates of a part); the search of the triangles starts from the one
  the last point was nearest to, and does each box distance once. The vertices are where they were (checked on a
  mesh against its own surface). Fields without such a part are not touched.
- **Dragging a surface of a shape that holds an oracle** (a mesh's distance, so the exact distance above, or a mesh
  import) pulled nothing: the evaluator that works out how the field changes with each `var()` gave the oracle the
  point of an earlier call, so the field was read at the wrong place, and let the oracle's gradient in space into
  the lanes that hold the derivatives with respect to the variables (an oracle only reads the point, so it does not
  change with them). Both are fixed; a drag of a thickened part now moves its surface.
- **A mesh (or any oracle) moved with `var()` numbers follows them.** The oracle that holds a transformed copy of
  another (`move(mesh, (var(5), 0, 0))`, a gizmo on an imported mesh, the exact distance of a part with a gizmo) took
  its numbers as 0 whatever the script said, so it stayed at the origin while the shapes made of arithmetic moved.
  The evaluators now pass every variable to their oracles (`Oracle::setVar`), and the application counts the
  variables inside an oracle among the ones a shape depends on, so such a shape renders again when one of them is
  dragged.

- **`import_step_tessellated_parts()` / `import_step_tessellated()`: a separate importer for parts that are almost all
  free-form (B-spline) faces** — sculpted bodies, gears, worms, threads, impellers — where the fitted closed-form
  surfaces of the main importer do not follow. Nothing is reconstructed or fitted: every solid is tessellated
  straight from its trimmed faces and the triangles are made the exact signed distance field of the part (the way
  nTop makes an implicit body of a B-rep). The parts, their order, names, units, bounds and assembly placements are
  those of `import_step_parts()`. The tessellation is kept in `<file>.fieldes-tessellation` next to the STEP file. Measured
  against OpenCascade on the test files (more than 260 parts): face areas agree to 0.05 to 0.1 %, volumes to 0.5 %; against
  the main importer the inside and outside of every part agree except four engine pistons (1 to 2 % of the points,
  where it matches OpenCascade). The whole of Keukencombinatie (90 parts, 412 free-form faces) imports in 22 s,
  Cribadora (111 parts) in 16 s, PT.stp in 7 s, the Bandextruder with its worm gear in 7 s, where the main importer
  takes minutes or fails on the free-form parts. Meshing a part takes 0.5 to 2.8 times as long as the main
  importer's (about as long for the worm gear).
  See [Importing STEP files](docs/step-import.md#parts-that-are-almost-all-free-form-import_step_tessellated_parts).
- **The tessellator of the exact STEP surface** (`exclude()` and the tessellating importer) is faster and more exact:
  the free-form faces of a solid are refined on all the threads there are (a worm gear: 31 s to 4 s); a cylinder or
  cone with windows that wind round it (helical grooves) is cut open along a helix (the face came out 79 % too big);
  holes of a face round a periodic surface are put where the outline is (a cross hole came out 76 % too big); a
  sphere's cap has circles of latitude (a ball of two hemispheres was two cones: half its volume); tori are refined,
  kept only where the mesh's area gets closer to the patch's own (fillets were 8 to 11 % too small); the holes of a
  free-form face that closes on itself are moved a period to where its outline is (two round holes of a 750 mm panel
  were not cut out: 5 % too much area and volume); the two circles of a zipped band start at the same place (a
  sphere's zone came out a twisted strip, 13 % too big).
- **A rational B-spline arc that is not circular** (a quarter ellipse of 250 x 100 mm has the weights of a circle's
  arc) was read as an exact circular cylinder or cone, in the main importer too. It is now checked at two more points
  and an ellipse becomes the free-form face it is. (Files with such faces are imported correctly after
  **↺ Reset** of the import; the import cache is not invalidated.)

### Analysis and design
- **Topology optimization warns when the optimised part falls into pieces.** A tetrahedral `topology_optimization` counts
  the separate pieces of its result (`.pieces`, specks under 2 % not counted) and, if there is more than one, prints
  "the optimised part is in N separate pieces ... try a higher volume_fraction". Kept regions (`keep=`, an excluded
  region) use up part of the volume asked for, so the free material has less; the loose scraps stay in the result on
  purpose, as they show what the part would become with more volume.
- **`smooth(shape, radius, steps=1)`**: smooths the surface of a body without thickening it. The body's field is averaged over
  the points a `radius` away on all six sides, `steps` times, so bumps, dents and stair-steps smaller than about the radius go,
  edges and corners are eased, and flat faces stay where they are (`offset` and `thicken` move or grow the surface); more
  `steps` smooth further. Beside `round_edges` (convex edges only) and `fillet` (concave only). **It is a field and nothing
  else**: a weighted sum of copies of the body's own field moved by whole radii (6 copies for one step, 19 for two, 44 for
  three, 85 for four; more steps are four at a larger radius), so no surface is measured or meshed inside it -- the mesher
  reads the field when it draws it, as for any other. It measured the distance with a mesh at every step before: a 20 mm
  box with a region excluded, `smooth(r=1, steps=3)`, took 75 s (4 s of script, 71 s drawing it). The arguments `bounds` and
  `resolution` are gone with the meshing. On an excluded shape it smooths the field the shape was made from, not the united
  one (whose values are cut off at the distance to the region), so the smoothed surface no longer ripples round the
  region; the locked part is put back as it was.
- Static, modal, thermal and thermal-stress analysis on body-fitted tetrahedra (or voxel hexahedra),
  structural and thermal topology optimisation, several load cases.
- **Fluid flow.** `fluid_analysis(domain, conditions, fluid, element_size, time=None)` solves the incompressible laminar
  Navier-Stokes equations through or around any shape (the fluid is where the field is negative) on the same
  body-fitted tetrahedra as the other analyses -- the steady flow, or with `time=(duration, step)` the flow in time
  from an impulsive start (backward Euler, every stored step a `FluidStep` with its fields and numbers, the result
  card stepping through them). `result.streamlines()` gives the paths of particles through the flow. The conditions are regions: `inlet(region, velocity= | speed= | flow_rate=,
  profile='uniform' | 'developed')`, `outlet(region, pressure)`, `wall(region, velocity)` (every other surface is a
  wall at rest), `slip(region)`; `Fluid(name, density, viscosity)` with `water`, `air`, `oil`, `glycerol`. The results
  are fields (`speed`, `vx`, `vy`, `vz`, `pressure`, `total_pressure`, `shear_rate`, `vorticity`) and numbers (the
  flows, the pressure drop, the force on the walls, the dissipation, the Reynolds number, how many elements lie
  across the passages). Linear velocity and pressure with SUPG / PSPG stabilisation, Picard then Newton iterations
  from the Stokes solution, BiCGSTAB with an incomplete LU (a direct LU for small systems); wall forces as the
  reactions of the discrete momentum equations. Verified against plane Poiseuille flow (pressure gradient within 1 %
  at 10 elements across), Hagen-Poiseuille pipe flow (within 1 % at 17 across), the Stokes drag on a sphere in a tube
  (within 2 % of Haberman & Sayre's wall factor) and the lid-driven cavity of Ghia, Ghia & Shin (1982) (extrema within
  5 % at Re 100 with 64 cells across, 7 % at Re 400 with 96) -- the numbers are in `docs/analysis.md`. Laminar only,
  first order in time, no boundary-layer elements: see its limits there.
  Example `16_fluid_flow.py`: water past a round post, the velocity field and the wake.
- **Flow topology optimization.** `flow_topology_optimization(body, domain, conditions, fluid, objective, volume,
  region, ...)` takes a body in a stream and changes its shape and topology for the least drag, the most lift, or a
  mix of both, under a volume bound (`volume=1.0` keeps the body's volume, `(0.5, 1.5)` bounds it), inside a
  region`, with `keep` / `avoid` regions and `extrude=`. The body is a level set at the mesh's nodes -- a smooth
  field whose zero level is the boundary, placed to a fraction of an element -- and each element's share of it (the
  exact fraction inside the boundary) sets the friction of a penalised solid (Borrvall & Petersson) in the real
  (Navier-Stokes) flow, solved once per iteration with its exact discrete adjoint. The result is the body
  (`.shape()`, after any iteration too; `.level` the field), the drag and the lift per iteration and the real flow
  around the final body (`.flow`); shown, it is the body in the flow with its wake, and the result card steps
  through the iterations. Measured on the round post of example 16 at
  Re 40: the optimiser's drag 45 % lower over 24 iterations (160 s), the real flow around the slender 21 x 4 mm body
  it makes 51 % below the post's. See `docs/analysis.md`. Example `17_flow_topology_optimization.py`.
- Flow analysis fixes found on the way, for every flow: an inlet or outlet in the middle of a flat face was one
  element wider than its region all round (the flat wall's triangles straddling the rim were taken as the rounded
  edge's); a given inlet speed counted the rounded-edge triangles at full area (+19 % flow on a thin slab) -- the
  inlet's area is now the patch projected along the flow; the rounded edge between two slip planes holds both
  normals; the incomplete LU is kept between solves while it still pays (a flow in time 4× faster per step).
- Fields and regressions: distances, maps, `fit()`, data fields, `colored()`; all results are fields.
- Lattices: nine TPMS families, a dozen strut lattices, planar patterns, Voronoi foams, surface and graph
  lattices, custom unit cells and equations, conformal cell maps, field-driven sizes.
- **A lattice is made of a cell, and every lattice operation takes one.** `cell_periodic(kind)` (a standard strut
  cell, TPMS or planar pattern), `cell_non_periodic(kind)` (`'voronoi'`, `'delaunay'`), `cell_custom(region, geometry)`
  (any geometry, see below), `cell_custom_truss(nodes, beams)` and `cell_custom_tpms(equation)` all make the same kind of object, and
  `lattice()`, `lattice_surface_conform()`, `strut_lattice()`, `tpms()`, `planar_lattice()` and
  `lattice_parameter_for_density()` take it as their `cell` — and only that: **the old forms are gone** (a name string
  such as `'gyroid'` in the cell's place, `kind=`, `surface=`, `unit_cell()`, `tpms_equation()`, `periodic()`,
  `voronoi_lattice()`, `surface_lattice()`); a refused call says what to write instead. `cell_non_periodic` also lays a
  random graph on a surface in `lattice_surface_conform()`.
- **`cell_custom(region, geometry)` is a cell of any geometry.** `region` is a box that gives the extent of the cell and
  `geometry` any field: the cell is their intersection, so the geometry may reach out of the box, and it repeats on any
  cell map (straight, cylindrical, spherical). `lattice(part, cell)` makes cells of the size of the region (`cell_size`
  scales them, `(sx, sy, sz)` stretches them); a region that is not a box is refused; `check=True` warns when the faces of
  the box do not match; `cell.solid()` is the cell on its own; a thickness, radius or density is refused (the geometry is
  the lattice). What `cell_custom` was before is renamed: **`cell_custom_truss(nodes, beams, mirror='')`** for beams between
  nodes and **`cell_custom_tpms(equation)`** for a TPMS equation; `cell_custom(nodes, beams)`, `equation=` and `shape=` say so
  when they are used. `examples/11_custom_lattice.py` shows all three.
- **`lattice_surface_conform(surface_field, cell, depth, cell_size, ...)` takes ONE surface argument**: a body, a
  surface (a field that is only zero on it) or a `select_surface()` selection — which of them it is is found out, not
  told. `within=` keeps the lattice inside any other shape (it replaces the old `surface=`).
- **`union(a, b, ..., radius=0)`** blends the surfaces where they meet with a smooth transition of that radius (mm),
  like the blend radius of a boolean in other tools; the default 0 is the sharp union. It works on any field, so
  lattices (also custom ones) are joined to a part with a fillet.
- **The render cache is on by default.** Every shape keeps its finished mesh on disk once it took 0.4 s or more to
  mesh (`render_cache(x)` keeps it however quick); the model tree's cache button now turns it **off** for a shape
  (it writes `x = render_cache(x, False)`, and deletes the line to turn it on again).
- **`select_surface(shape, seed, angle, mode, thickness, radius)`**: the flood fill of a CAD program as a field —
  the patch of the surface around a point, found over the triangles of the shape's surface mesh, as a thin layer
  that is a region for `fixed()`, `force()`, colouring and lattices. `.patch` and `.whole` are the unsigned
  distances to the patch and to the whole surface.
- **`lattice_surface_conform(surface_field, cell, depth, cell_size, ...)`**: a conformal lattice as in nTop. Its cells
  lie on the surface — a selected face, or the surface of the body — as big as asked along it, each with a face
  towards the surface normal; by default it **fills the body** (one layer for a thin shell), with `side='outside'` the
  layers stand out of it. It is made **from the body's field alone** (the surface is where the field is zero, its
  normal the gradient; no mesh, no distance to a mesh). **The cell map is a set of quads on points of the surface that
  covers the whole of it and is watertight** (every edge on two cells, every vertex one fan, one orientation), made by
  one of **two layouts**, chosen by looking at the surface: a **closed body** gets a layout made from the graph of its
  surface points (a global field of four directions, vertices where the field is singular and on a grid, the surface
  divided into one region round each vertex, a face wherever three regions meet, every face of k corners cut into k
  quads), and a **sheet** (a surface that goes on beyond the region, or has an edge) gets a layout from a scaffold of the
  surface cut by the region. Nothing of the surface is left out: a piece too small for the cells is sampled finer on its own,
  a point has no area and is no surface, and **a closed map of the whole surface is delivered even where the cells cannot
  resolve a part** (a hole or a gap narrower than a cell, a wall thinner than one): the layout is made ONCE, from the grid of sample
  points that `grid_offset` (0 to 3, a setting of `lattice_surface_conform`) picks, and the warning the call prints says where its
  topology may be off. A map is refused only when that grid gave no closed one, and the error says to try another `grid_offset` or a
  smaller `cell_size`: the layout does not try other grids behind your back. The field is evaluated by all the processor's
  threads at once (the same layout whatever their number): an imported bracket of 150 x 120 x 88 mm at 5 mm cells (2650 cells) is laid out
  in 28 s on 12 threads (about 50 s inside the application window), where one thread and two layouts took 154 s. The cells are then
  made regular by moving their nodes along the surface (the cells stay the same cells); a cell is bad when a corner is under 40°
  or over 140° or its longest side more than 2.5 times its shortest: 30-45 % of the cells of a fresh layout are bad, 4-10 % after the
  smoothing. **What must not be in a map is counted exactly:** a cell folded over a neighbour or turned over, one lying on top of
  another, an edge through the air, two corners at one place; the call's warning gives their number, and says when a wall, gap or hole
  thinner than the cell makes up a share of the surface (such a feature can be missing from the map). **Sharp edges and corners are followed**: the samples next to an edge or a corner are moved onto it (the way dual contouring finds its vertices: the planes of the neighbouring samples are fitted, and a move is kept only if the field says the point is on the surface), so the lattice line along an edge is exact and the outline of a part is straight; a corner of the part is always a lattice vertex, with the lattice pinned to it; the face centres are never on an edge; the nodes that sit on an edge or a corner are read from the field and stay there, and the nodes on a straight edge spread evenly along it. The smoothing has a second phase that draws every other node towards the mean of its neighbours (rows next to an edge relax into straight lines). On the test parts with sharp edges 82 % of the vertices next to an edge were off it before, 5 % are now. **Cells are distorted to fit and never deleted for it** (there is no
  `max_stretch`): over a fillet, round the lip of a rim, the cell bends with the surface — its inside is found from its four
  edges, which run along the surface. **The material is filled once**: on a body thinner than its layers (a shell with two
  surfaces, an inner and an outer skin) both faces of a slab are mapped, and a cell is left out only when every point of its
  column that lies in the material is also in the column of another cell that stays (the smaller surface piece goes first), so a
  pan skin has one layer, not two, and nothing it filled is left bare (the material inside no column falls by 0.4 to 2 points). The beams of the unit cell are carried over
  to every cell, so the lattice is a graph of beams that renders about as quickly as one. Strut cells (`octet`, `bcc`,
  `cubic`, `kelvin`, …) with `radius`; or **a periodic surface that follows the surface** (below). No `prism` / `xbrace` /
  `zigzag` trusses here (they needed a mesh of the surface). (The earlier versions — a flat grid across the face, fast marching
  over a mesh of the surface, columns walked from the curve where a plane cuts the surface, a square grid grown as a front, and
  rings grown round one point (with `origin=`) — all left cells that did not close or deleted cells where fronts met, so all are
  gone. The `lattice_surface_confirm` spelling, a typo, is gone.)
  A field that is only a **surface** (zero thickness: where it is zero) gets one layer on one side of it (`side`), cut off at the edge of the shape given as `within=`; `bounds=` gives the extent of a field that has none. Examples `14_conformal_lattice.py` (a surface) and `15_conformal_closed_body.py` (a closed body). **Measured** on 372 runs on bodies the program had not seen (random CSG bodies, mechanical-looking bodies, plates with bosses, holes and pockets, six STEP parts; two cell sizes each; the box a body gets when none is given), against a count of the topology (the Euler number) of the field on a voxel grid taken at three resolutions: of the 229 runs whose count is trustworthy the map has the right topology in 213 (93 %) and a wrong one in 16 (7 %: a hole or a wall about as thin as the cell is missed or invented; two of the 16 are an island of 0.14 mm³ in the pan's field that the map does find and the voxel count cannot see); 46 runs (12 %) were refused with the message above; 97 more gave a closed map of a body whose count is not trustworthy (features thinner than a voxel). Known limits: a feature thinner than a cell can be lost or invented silently when the placements agree on the wrong answer; a thin tube (a 3 mm wall at 6 mm cells) can be refused where half the cell works; about a fifth of the vertices have a valence other than four (a quad layout of a closed surface needs some); a layer that is deeper than a thin plate stands out of it and is taken back into the material, and 1 to 2 % of the material is in no layer's column at the rims.
- **`lattice_surface_conform` with a periodic surface** (`cell_periodic('gyroid')`, `'schwarz_p'`, `'diamond'`, `'neovius'`, `'lidinoid'`,
  `'split_p'`, `'iwp'`, `'frd'`, `'fischer_koch_s'`; `thickness`, `style='sheet'|'network'`, `offset`, `invert`, `skin`): a TPMS
  that follows the surface, one period to each cell on it and to each layer through the depth — nTop's conformal TPMS, and
  a textured periodic surface for a whole part. It is evaluated in the coordinates of the cell a point is in, found from
  the cell's four edges (a Coons patch) and the depth of the layer by Newton's method, behind a bounding-volume hierarchy
  of the cells; the field is a pure function of position and made from the body's field alone, like the struts.
  The cosine-only surfaces (Schwarz P, Neovius, IWP, FRD) join up across the borders of cells; the others may show a seam there.
- The periodic surface on the cells is quicker to evaluate (a cell whose box holds a point but whose map does not is rejected
  after one step, not twelve: 40 → 13 µs a point in a deep layer, a third off the render of the S-surface example), and it is
  kept by the render cache under a hash of the cells rounded to a thousandth of a millimetre: a layout that changes has other
  cells and so another key, the cache never gives the mesh of other cells.
- A point of a flat face has the same gradient in every run: where two parts of a field are equal (a point exactly on a face
  of `box_exact`) the evaluator gave the gradient of whichever it met first, a different one in different runs; a lost gradient
  now comes from differences of the field round the point, and the search grid is shifted off round numbers.
- Automation (developer facility): `waitrender [seconds]` waits for the render to finish before the next command.
- **Two conformal examples**: `14_conformal_lattice.py` (an open surface, with a `cell_custom_truss` cell of your own: a custom cell
  follows a surface when its beams share one radius and it is mirror-symmetric) and `15_conformal_closed_body.py` (a whole bracket).
- **`thickness` sizes every cell**: the wall of a sheet TPMS and the diameter of the beams of a strut or non-periodic cell, in `lattice()`,
  `strut_lattice()` and `lattice_surface_conform()` (before, `lattice_surface_conform` took no thickness for beams, and `strut_lattice` only a
  radius); `radius=` stays as half of it, and both together is an error.
- The conformal lattice is the same in every run (its sample grid is fixed by `grid_offset`).
- **`Shape.save_stl` crashed now and then** (a third of the time on a big mesh): the C function reads trees until
  a null pointer and the Python call did not give it one.
- **Render cache** (keys: the tree as built, meshes by their surface; `render_cache_key(shape)` shows them). A button on every row of the model tree (and `render_cache(shape)` in the script, the line it
  writes) keeps the shape's finished mesh on disk. The same shape — the next time the script is opened, or after
  it was changed and changed back — is shown from there at once; when anything about the math changes (an
  operation, a number, a dragged `var()`, an imported file, the region, the resolution) the shape is meshed again as
  usual and the new mesh is kept. Off unless turned on; shapes that depend on a solved analysis cannot be kept (the
  button turns amber). The folder is limited to 4 GB; Settings → Clear the caches.
- The Python meshing and evaluation functions take the script's `var()` numbers (`libfive_tree_render_mesh_vars`,
  `libfive_tree_eval_points_vars`), so a shape with variables meshes the way the viewport shows it.
- **Dual contouring no longer makes spikes.** A vertex may leave its cell to keep a sharp feature, but a vertex
  more than a cell width away (thin walls only a few cells thick, creases at a nearly flat angle) went hundreds of
  millimetres off; it is now pulled back to the cell widened by its own width.
- Mesh import (STL, OBJ, PLY, 3MF, glTF) as exact distance fields.
- Content-addressed caches for imports, analyses, exact distances and graphs.
- **One cache for every field.** A statement `name = <expression>` whose value is a field (a conformal lattice on a
  bracket, an offset, a thickness field) is remembered by its text, the exact content of what it reads, the `var()`
  numbers and the code that builds it. The same statement is not computed again — in the session, and (when every part
  of the field can be saved) after closing and opening FielDes: the 30 s lattice of the bracket example is read in
  0.15 s with identical values. Lattice struts (`BeamLattice`) now save and load; `libfive_tree_can_save` tells whether
  a tree can be written, and `libfive_tree_save` refuses one that cannot instead of writing a file that fails to load.
  **Settings → Clear the caches** deletes the field cache with the kept meshes.
- **One progress bar for a whole conformal lattice.** A call of `lattice_surface_conform` is one bar from the first step
  to the last (17 steps for a closed body, 10 for an open surface), so the line of the script that makes it shows how
  far it is. The text names the step (`step 6 of 17: joining the samples into a surface`, the round or level inside it);
  every step has its share of the bar (measured on the bracket and the pan), and inside a step the bar counts what has
  really been done. The bar never goes back; a second attempt of the layout takes the rest of it.

### Known limitations
See the README: fitted (not exact) B-spline faces in the field; tori not refined in the exact mesh;
Windows is the only tested platform; linear analyses without contact.

Meshing a field that contains `normal_field()` or `gradient_field()` (the gradient oracle) can crash or hang: its
interval bound is unbounded.

**The mesh of `shell_outside` / `shell_inside` (and of the exact distance itself) of an imported part has cracks, and
is slow.** The exact distance is the distance to a mesh of the part; meshing the shell (a field with that oracle in
it) at 2 samples per mm leaves about 14,000 open edges for a frying pan, along the first planes the mesher splits its
region at, and takes 36 s against 0.6 s for the part's own field; the mesh changes with a sub-voxel shift of the
region and between runs. `thicken` of the same part (surfaces at plus and minus half the thickness only, none on
the distance's own zero set) is clean (0 open edges, 3.6 s). Not fixed: it is the exact distance's, which is made
from a mesh and so breaks the rule that the mesh is only the output. Until then: `thicken`, or `lattice_surface_conform`
on the part itself, not on a `shell_outside` of it.

The field of an imported part is exact in sign (inside or outside) and a lower bound of the distance to the part:
outside the part it is the distance to the infinite surfaces the part's faces lie on, so what reads it as a distance
(the `depth_below` of a lattice skin, a field-driven design, `exclude` regions made from it, a union of parts) sees
that: blocks and fins where a face ends before its surface does, sharp plane-to-plane edges. `thicken`, the shells
and the offsets of a part use its exact distance instead (see above), which costs 2 to 3 times the render of the
part's own field. Making the field itself a true distance (a box around each plane's faces, round corners, the
distance to the faces themselves) was tried several ways and made the render of every part several times slower, so
the field stays the fast one. Offsets and shells of an imported part that was combined with something else are
made from the combination's own field: use `exact_distance()` on it.
