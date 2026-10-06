# Runs a script text the way the application does (fieldes.runner.run) and prints the error, if any
import os
import traceback
from fieldes import *
from fieldes import runner

path = os.environ.get('SCRIPT_TEXT') or 'C:/Users/tinmi/OneDrive/FielDes/FielDes/dev/logs/tree_e_script.txt'
text = open(path, encoding='utf-8').read()
try:
    out = runner.run(text)
    print('ran', len(out), 'statements')
except Exception:
    traceback.print_exc()
print(runner.last_output)
