'''
The language of the words the Python side shows the user (the labels of the creation menus, a few messages), as the program's own
words are (see app/include/fieldes/i18n.hpp): the English text is the key, `tr('text')` is that text in the language the program runs
in -- the English text itself where there is no translation -- and what is not fixed in it is %s or %(name)s, filled in after the
translation:  tr('%s needs a second model') % name

The translations are `translations/<code>.json` beside the program ({"English text": "text in that language"}); the language is
FIELDES_UI_LANGUAGE, which the program sets from its setting when it starts (FIELDES_LANGUAGE forces one, for a script run on its own).  The names of the library's functions and arguments are
code and stay as they are.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import json
import os

__all__ = ['language', 'tr']

_loaded = {}


def language():
    ''' The code of the language in use ("en" when none is set) '''
    return os.environ.get('FIELDES_UI_LANGUAGE') or os.environ.get('FIELDES_LANGUAGE') or 'en'


def _folder():
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = []
    if os.environ.get('FIELDES_DIR'):
        candidates.append(os.path.join(os.environ['FIELDES_DIR'], 'translations'))
    candidates.append(os.path.join(os.path.dirname(os.path.dirname(here)), 'translations'))         # (the source tree: python/fieldes/..)
    candidates.append(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(here))), 'translations'))
    for c in candidates:
        if os.path.isdir(c):
            return c
    return candidates[0]


def _table(code):
    if code not in _loaded:
        table = {}
        if code != 'en':
            try:
                with open(os.path.join(_folder(), code + '.json'), encoding='utf-8') as f:
                    table = json.load(f)
            except (OSError, ValueError):
                table = {}
        _loaded[code] = table
    return _loaded[code]


def tr(text):
    ''' `text` in the language of the program, or `text` itself when it has no translation there '''
    if not text:
        return text
    return _table(language()).get(text) or text
