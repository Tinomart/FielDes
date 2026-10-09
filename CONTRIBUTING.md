# Contributing

Thanks for looking. FielDes is a first beta, so reports of what does not work are as valuable as patches.

## Reporting a problem

Include:

- what you did, and what you expected;
- the **script** (or the smallest part of it that shows the problem) and, for an import problem, the STEP
  file if you may share it;
- the **output pane** text (a traceback, or the fit report) and your Windows version;
- a screenshot (`Ctrl+Shift+E`) for anything visual.

## Building and testing

See [docs/building.md](docs/building.md). There is no automated test suite yet; the checks used so far are:

- `python scripts/run_example.py examples/<n>.py` for every example (they must all finish without error);
- opening each example in the application and looking at the viewport;
- for the STEP exact mesher, comparing the volume and watertightness of `exclude()` pieces with another
  CAD kernel on a set of parts.

A test suite that automates those is the most useful contribution there is.

## Code

- The kernel (`kernel/`) and the Python library (`python/`) are MPL-2.0; the application (`app/`) is GPL-2.0-or-later.
  New files carry the header of the part they belong to; see [NOTICE.md](NOTICE.md).
- Match the surrounding style: short comments that say *why*, no dead code, no opt-in flags for rejected
  behaviour.
- Docstrings in `python/fieldes/stdlib` become [docs/reference.md](docs/reference.md): run
  `python scripts/gen_reference.py` after changing one.
- The documents in `docs/` describe behaviour; change them with the behaviour.
- Text the user reads is `T("English text")` in `app/src` and `tr('English text')` in `python/fieldes` (the English *is* the key; `%1`, `%2` / `%s` for what
  varies). After adding or changing one, run `python scripts/translations.py check`: it lists what the six translations lack.
  Better translations are welcome: see [translations/README.md](translations/README.md).

## Ideas that would help

Linux/macOS builds, a test suite, contact in the analyses, more STEP entity types, exact B-spline faces in
the field, a package manager recipe for the dependencies.
