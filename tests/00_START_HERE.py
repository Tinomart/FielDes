# START HERE: the tests of FielDes 1.0.0.
#
# This file does nothing but list the others.  Every test is a script you open in the window (File > Open, or drag it onto the window) and look at: it builds something, and
# either prints one PASS or FAIL line per check in the output pane (manufacturing) or tells you in its first lines what to do with the mouse and what you must see (the user
# interface).  Look at the picture first; the numbers are an aid.
#
# 1.  THE INTERFACE (tests/ui): the features you work with by hand.  Five small scripts, each one opens with WHAT TO DO and WHAT YOU MUST SEE.
#       ui/01_primitives_and_their_handles.py     every primitive of the Primitives menu (eight solids, six flat shapes): pull a face, and only that face's number changes
#       ui/02_python_in_the_model_tree.py         a script of plain Python (loop, if / elif / else, def, class, try, while): the tree shows it, its parts are locked, a block
#                                                 moves by drag, the Python menu writes plain code
#       ui/03_the_insertion_point.py              click between two rows of the tree: what you add lands there; the point and the text cursor follow each other
#       ui/04_extrude_with_ctrl_e.py              Ctrl+E extrudes the selected 2D shape; the faces of an extrusion pull; a 2D shape's edge wins over a face where they overlap
#       ui/05_the_part_turned.py                  a manufacturing result whose way up was left free is drawn standing the way it is printed
#
# 2.  MANUFACTURING (tests/manufacturing/00_START_HERE.py): the check of a part against what a process asks of it, and the tool that changes the part until it keeps the rules.
#     One file for each thing that can happen (98 of them), the three samplers, the job cases (a topology-optimized bracket made ready for a printer, a mill and a mould),
#     and parts the code has never seen.  Its own START HERE says what each file tries to break and how long it takes.
#
# 3.  THE KNOWN ANSWERS (examples/verification/v1 .. v4): four problems with a solution on paper (a cantilever, a plate with a hole, a channel flow, a bar's heat conduction):
#     the script stops with an error when FielDes is farther from the answer than a stated tolerance.
#
# 4.  THE EXAMPLES (examples/01 .. 33): each one is a small design that should open, run and draw.
#
# THE REPORT: tests/RELEASE_REPORT.md says, for every feature of this release, what it does, how it was tested, what it showed, and what is not as good as it should be.

print("Open one of the files listed above: tests/ui/01 .. 05 for what you do with the mouse, tests/manufacturing/00_START_HERE.py for the manufacturing tests.")
