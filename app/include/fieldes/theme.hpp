/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once
#include <QColor>
#include <QString>

class QApplication;

namespace FielDes {

/*
 *  The look of the application.  The window around the viewport is light -- white bars,
 *  a pale warm background, dark grey-blue text -- and the viewport and the cards floating
 *  in it (model tree, section card, legends, result card) are the dark teal of the
 *  Solarized palette: the model is the one dark thing on the screen.
 */
namespace Theme
{
    extern const QColor chrome;      // window background, between the bars
    extern const QColor surface;     // bars, menus, dialogs' fields
    extern const QColor paper;       // the editor
    extern const QColor gutter;      // the editor's line-number column
    extern const QColor border;      // hairlines
    extern const QColor text;        // text on light
    extern const QColor muted;       // secondary text on light
    extern const QColor accent;      // selection, focus

    /*  Fusion style, the light palette and the application's style sheet  */
    void apply(QApplication& app);
}

}   // namespace FielDes
