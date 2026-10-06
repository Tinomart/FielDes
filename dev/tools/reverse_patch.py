# Undoes a patch script of this repository: it runs the script's own `sub(old, new)` calls without applying them, records them,
# and applies them backwards (new -> old, last first).  usage: python reverse_patch.py <patch.py> [<patch.py> ...]
import os
import sys

recorded = []        # (path, old, new)
current = {'path': None}


def run(patch_file):
    text = open(patch_file, encoding='utf-8').read()

    def load(path):
        current['path'] = os.path.abspath(path)
        s = open(path, newline='', encoding='utf-8').read()
        return s.replace('\r\n', '\n'), '\r\n' in s

    def save(path, s, crlf):
        pass

    def sub(s, old, new, count=1):
        recorded.append((current['path'], old, new))
        return s

    glob = {'__name__': 'recording', 'load': load, 'save': save, 'sub': sub, 'os': os}
    # (the script defines its own load / save / sub: its definitions are replaced by the recording ones after they are made)
    code = compile(text, patch_file, 'exec')
    ns = dict(glob)
    # run the module body but keep our helpers: the definitions are skipped by removing the `def` blocks from the text
    import re
    stripped = re.sub(r'^def (load|save|sub)\(.*?(?=^\S)', '', text, flags=re.S | re.M)
    exec(compile(stripped, patch_file, 'exec'), ns)


for f in sys.argv[1:]:
    run(f)

by_path = {}
for path, old, new in recorded:
    by_path.setdefault(path, []).append((old, new))
for path, pairs in by_path.items():
    raw = open(path, newline='', encoding='utf-8').read()
    crlf = '\r\n' in raw
    s = raw.replace('\r\n', '\n')
    for old, new in reversed(pairs):
        assert new in s, (path, new[:100])
        s = s.replace(new, old, 1)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'w', newline='', encoding='utf-8').write(s)
    print('reversed', len(pairs), 'edits in', os.path.relpath(path))
