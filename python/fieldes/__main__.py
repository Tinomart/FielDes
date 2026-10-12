'''
python -m fieldes run APP [--name value ...]    run a FielDes app without a window (the same as `FielDes run ...`)
python -m fieldes export SCRIPT [-o FILE]       bundle a script into an app
python -m fieldes info APP                      what an app asks for

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import sys

from fieldes import app_package

if __name__ == '__main__':
    sys.exit(app_package.main(sys.argv[1:]))
