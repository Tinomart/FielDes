# A FielDes app: a script that someone who never opens the editor can run.
#
# An engineer writes the script once and says, with `app.`, what the person who runs it may choose.  FielDes shows those inputs
# as a form -- a window of its own, with a Run button, a log and the files that were made -- so an artist or a designer opens
# the app, fills in the form and presses Run.  Nothing of the editor, the model tree or the code is in the way.
#
#   in the editor   every input is just its default (`app.number('Wall thickness', 3)` is 3): the script runs as any script does,
#                   and is tried and changed here.  Press File > Preview as app to see the form as the person will.
#   File > Export as app...   bundles the script, the custom blocks it uses and the files it needs (the bracket below, brought
#                   in by app.asset) into ONE file, bracket_maker.fieldesapp.  Send that file.
#   to open it      a double click opens it in the form (FielDes offers to take over .fieldesapp files the first time you export one;
#                   Settings > Open .fieldesapp files with FielDes does it any time).  Run it as a program too:
#
#                       FielDes run bracket_maker.fieldesapp --Mode Hollow --Wall_thickness 2.5 --Export_path C:\parts\bracket.stl
#                       FielDes run bracket_maker.fieldesapp --watch      runs again by itself when the model file changes
#
# What to write (every input is written with plain values at the top of the script: the form is read from the text):
#
#   app.title(name, description, version)                         the heading of the window
#   app.number(name, default, min=, max=, step=, unit=, help=)    a number (a whole number if the default is one)
#   app.text(name, default)                                       a text
#   app.flag(name, default)                                       a check box
#   app.choice(name, [options], default)                          one of a few
#   app.file(name, default, filter)                               a file to read
#   app.save_as(name, default, filter)                            where the result is saved (listed under Outputs afterwards)
#   app.folder(name, default)                                     a folder
#   app.asset('step/part.step')                                   a file that comes along inside the app
#   app.output('report.txt')                                      a file the script makes (listed under Outputs)
#   app.in_app()                                                  True when run as an app, False in the editor: write files only then
from fieldes import *

app.title('Bracket maker', 'Hollows a part out (or grows it) by a wall thickness and saves it as a mesh, ready to print', '1.0')

model = app.file('Model', app.asset('step/PivotBearingSupportBracket.STEP'), 'STEP files (*.step *.stp)',
                 'The part to work on.  The bracket that comes with the app is the default.')
mode = app.choice('Mode', ['Hollow', 'Thicken'], 'Hollow', 'Hollow: the part keeps a skin of that thickness.  Thicken: the part grows by it.')
wall = app.number('Wall thickness', 3, min=0.5, max=10, step=0.5, unit='mm', help='From 0.5 to 10 mm')
target = app.save_as('Export path', 'bracket_maker.stl', 'Mesh files (*.stl *.obj *.ply *.3mf *.glb)',
                     'A relative name is saved in your Documents folder')

# The rest is the script of any FielDes user: the inputs above are plain values in it.
parts = import_model(model)
bracket, (low, high) = parts[0]

view.set_bounds(*roi(parts), auto_scale=True)
view.set_resolution(roi_resolution(parts))
view.set_quality(8)

result = shell(bracket, wall) if mode == 'Hollow' else offset(bracket, wall)
result

# In the editor the script only shows the result: every change of it runs it again, and it should not write a file each time.
# As an app (File > Preview as app, a double click, FielDes run) it writes the file.
if app.in_app():
    export_mesh(result, target)
    print('%s %.1f mm: saved %s' % (mode, wall, target))
