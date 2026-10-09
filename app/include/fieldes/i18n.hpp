/*
FielDes: field-driven design

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <QList>
#include <QString>

namespace FielDes {

/*  The language of the program's own words (menus, buttons, messages, the guide and the tour).  The English text IS the key: T("Select a
 *  model first.") is that text in the language the program runs in, and the English text itself where there is no translation (so a text
 *  that is not translated yet reads in English, never as a code).  What a text holds that is not fixed -- a name, a number -- is %1, %2..., filled
 *  in after the translation: T("%1 is already in %2").arg(a, b), because another language puts the words in another order.
 *
 *  The translations are `translations/<code>.json` beside the program: {"English text": "text in that language"}.  The language is the
 *  setting `language` ("system" -- the language of the computer, when there is a translation of it -- or a code), read once, when the
 *  program starts: a change takes effect the next time. */
namespace i18n {

struct Language
{
    QString code;           // "en", "es", ...
    QString name;           // its name in itself: "Español"
};

/*  The languages the program has translations of, English first  */
const QList<Language>& languages();

/*  The code of the language of the computer when it is one of those, else "en"  */
QString systemLanguage();

/*  The setting: "system" or a code  */
QString chosen();
void setChosen(const QString& code);

/*  The language the program runs in (the setting, with "system" resolved)  */
QString current();

/*  Loads the translations of the language in use; called once, before any window is made  */
void load();

/*  The folder the translations are in  */
QString folder();

}   // namespace i18n

/*  The text in the language of the program (see above).  `english` must be a literal, or a text that is one of the keys  */
QString T(const char* english);
QString T(const QString& english);

}   // namespace FielDes
