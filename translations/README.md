# Translations

FielDes shows its own words (menus, dialogs, tooltips, the guided tour, the guide, the creation popups, the result legends) in the language
of the computer, or in the one chosen under **Settings → Language**. The language is read when the program starts, so a change comes
with a restart (the menu offers it).

| File | Language |
|---|---|
| (none) | English — the texts in the program *are* the English version |
| `es.json` | Español |
| `fr.json` | Français |
| `de.json` | Deutsch |
| `ja.json` | 日本語 |
| `zh.json` | 简体中文 |
| `ru.json` | Русский |

Each file is one JSON object: `{"English text": "text in that language", …}`. The English text is the key, so a text that has no entry
(or an empty one) simply reads in English; a new feature never shows a blank where its translation is missing.

What stays in English: the names of functions and arguments (they are code), the error messages and notes the Python library prints
about a script, the written documentation (`docs/`), and the command line help.

## Placeholders

`%1`, `%2` … in a text are filled in by the program (a name, a number). A translation keeps the same ones, in any order the language needs
("`%2` of `%1`"). In the Python side's texts they are `%s`, `%d`, `%.4g` or `%(name)s`, and those are **positional**: the program fills them in
the order of the English text, so a translation keeps that order (`check` compares which ones there are, not their order). `%%` is one percent sign. The tags in the texts
(`<b>…</b>`, `<br>`, `<a href="…">`) stay as they are.

## Adding or improving a language

1. Copy `es.json` to `<code>.json` (two letters, `sv` for Swedish) and translate the values.
2. Add the language to the list in `app/src/i18n.cpp` (`languages()`), and to `installer/Setup.cs` (`Texts`) if the installer should offer it.
3. Check the file:

   ```
   python scripts/translations.py check sv
   ```

   It lists the texts the file lacks, the ones it has that the program no longer asks for, and the ones whose placeholders differ from the English.

`python scripts/translations.py list` prints every text of the program that can be translated; `merge <file.json> <code>` puts the entries of
a file into `translations/<code>.json`, refusing the ones whose placeholders do not match.

To see which texts of a running program are still missing a translation, start it with the environment variable `FIELDES_I18N_LOG=1`
and the language set (`FIELDES_LANGUAGE=de`, which forces that language whatever the setting says; the program itself never sets it): each
missing text is written to the console once.

Qt's own words (the **Yes** / **Cancel** of a question, the **Copy** / **Paste** of a text field) are in the same files under keys that start with
`qt:` (`qt:QMessageBox|Show Details...`).
