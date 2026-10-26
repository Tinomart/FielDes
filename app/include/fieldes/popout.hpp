/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>

class QEvent;
class QToolButton;
class QWidget;

namespace FielDes {

/*
 *  Any major element of the window can be pulled out into a window of its own and put back: the viewport, the code editor, the console and output, the model tree and every card (the
 *  work plane, the symmetry, the section, the result ...).  `attach` gives an element a small button in its top right corner; pressed, it takes the element out of the window it is in -- a
 *  nothing stands in its place, the panes round it take its room (a hidden, empty widget only holds the place it goes back to) -- into a window of its own (owned by the main window: it
 *  closes with it, and it can be put on another screen); pressed again (its button is in its corner, in its window), or with that window closed, the element goes back to the place it came
 *  from, the sizes of the panes round it as they were.
 *
 *  What a window of its own keeps: the shortcuts of the window it came from (the actions of the main window that work anywhere in it, and those of the element's parent), so that a key
 *  does what it did; its place and size (kept by the key); and, when the program ends with it pulled out, the fact that it was (the elements are put back first, and pulled out again at
 *  the next start; the fact is cleared as it is read, so a start that crashed with one pulled out does not repeat itself).
 *
 *  An element that is pulled out has the property `floating`: the card controller (carddrag.hpp) leaves it where the window's layout puts it, and the viewport does not place
 *  anything by its geometry.  The viewport is the one element that is a graphics widget: a new window is a new graphics context, and View::releaseGL lets go of what the old one held.
 */
class PopOut : public QObject
{
public:
    /*  `target` gets the button.  `title` names it (the window's, the tooltip's); `key` is its name in the settings and in the scripted tests.  `nextTo`: a widget
     *  of the element, in its top right corner (a close button), that the button stands to the left of  */
    static PopOut* attach(QWidget* target, const QString& title, const QString& key, QWidget* nextTo = nullptr);

    static PopOut* find(const QString& key);
    static QStringList keys();
    static QStringList floatingKeys();
    /*  Every element that is pulled out goes back (`remember`: and is pulled out again at the next start); the window is about to close  */
    static void dockAll(bool remember);
    /*  The elements that were pulled out when the program last ended are pulled out  */
    static void restoreFloating();
    static bool isFloating(const QWidget* w);

    bool floating() const { return !m_window.isNull(); }
    const QString& key() const { return m_key; }
    void toggle();
    void floatOut();
    void dock();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    PopOut(QWidget* target, const QString& title, const QString& key, QWidget* nextTo);
    void placeButton();
    void refreshButton();
    bool onPaper() const;
    QWidget* makePlaceholder();
    void saveWindowGeometry() const;

    QWidget* m_target;
    QString m_title, m_key;
    QPointer<QWidget> m_nextTo;
    QToolButton* m_button = nullptr;
    bool m_hover = false;           // (the mouse is on the button: its ink comes forward)

    // Where it was: the window that holds it now, and the place it left
    QPointer<QWidget> m_window;
    QPointer<QWidget> m_parent;
    QPointer<QWidget> m_placeholder;
    int m_kind = 0;                 // 0: a card placed by hand in its parent, 1: a pane of a splitter, 2: an item of a layout
    int m_index = 0;                // (a splitter's pane)
    QList<int> m_sizes;             // (and the sizes of its panes)
    QRect m_geometry;               // (a card's place)
    bool m_wantVisible = true;
};

}   // namespace FielDes
