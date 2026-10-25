/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once
#include <QColor>
#include <QList>
#include <QString>

class QApplication;

namespace FielDes {

/*
 *  The look of the application.  Two sets of colours, light and dark:
 *
 *  Light: the window around the viewport is light -- white bars, a pale warm background, dark grey-blue text -- and the viewport
 *  and the cards floating in it (model tree, section card, legends, result card) are the dark teal of the Solarized palette: the
 *  model is the one dark thing on the screen.
 *  Dark: the window is dark as well (a dark teal window, the editor in Solarized's darkest base), the viewport and its cards as they are.
 *
 *  Each of the eight colours below can be chosen by hand, for each set (Settings > Theme): what is not chosen is the default of the set.
 *  The choice is kept in the settings; the Window changes the look of what is open with `apply` and `Editor::applyTheme`.
 */
namespace Theme
{
    enum class Mode { Light, Dark };

    extern QColor chrome;      // window background, between the bars
    extern QColor surface;     // bars, menus, dialogs' fields
    extern QColor paper;       // the editor
    extern QColor gutter;      // the editor's line-number column
    extern QColor border;      // hairlines
    extern QColor text;        // text on it
    extern QColor muted;       // secondary text
    extern QColor accent;      // selection, focus

    Mode mode();
    bool dark();

    /*  The colour `t` of the way from a to b (0: a, 1: b)  */
    QColor mix(const QColor& a, const QColor& b, qreal t);

    /*  What the eight colours give: the border of a field, a pressed button, the selected text of the editor, the page of a file that is not
     *  run, the handle of a scroll bar, the line under a tab, a disabled text, what a button of the editor shows under the cursor  */
    QColor field();
    QColor pressed();
    QColor selection();
    QColor aux();
    QColor handle();
    QColor handleHover();
    QColor tabLine();
    QColor disabled();
    QString hoverShade();

    /*  The eight colours, as the dialog lists them  */
    struct Role
    {
        QString key;
        QString label;
    };
    QList<Role> roles();
    QColor color(const QString& key);                          // as it is now (chosen, or the default of the set)
    QColor defaultColor(const QString& key, Mode mode);
    bool isCustom(const QString& key);                         // chosen by hand, in the set that is on
    bool anyCustom();

    /*  The choice kept in the settings (once, before `apply`)  */
    void load();
    /*  Changes the set / a colour / back to the defaults; `save` false leaves the settings alone (a test)  */
    void setMode(Mode mode, bool save = true);
    void setColor(const QString& key, const QColor& colour, bool save = true);
    void resetColor(const QString& key, bool save = true);
    void resetColors(bool save = true);

    /*  Fusion style, the palette and the application's style sheet, from the colours as they are now  */
    void apply(QApplication& app);
}

}   // namespace FielDes
