/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

class QWidget;

namespace FielDes {

/*
 *  The cards that float over the viewport (the model tree, the section card, the field viewer, the result card) can be dragged
 *  anywhere inside it and resized from their edges and corners.  A CardController gives one card that: it is dragged by its header
 *  (the `handle`: a click on it still works) or by any empty place of the card, and resized from the margin round it.  Where a card is
 *  and how large is kept relative to the window (it stays at the same place along the free space when the window is resized) and in
 *  the settings, so that it is there the next time.  A card that was never moved or sized keeps its own default place and size:
 *  `moved` and `sized` say what the user did.
 */
class CardController : public QObject
{
    Q_OBJECT
public:
    CardController(QWidget* card, const QString& key, QWidget* handle = nullptr, QSize minimum = QSize(220, 60));

    static bool moved(const QWidget* card);
    static bool sized(const QWidget* card);

    /*  A card that was sized by the user keeps its size when it is collapsed to its header and when it is opened again  */
    static void collapse(QWidget* card, bool collapsed, int collapsedHeight);

    /*  The card, put back where the user left it (and kept inside the viewport)  */
    void restore();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    enum Edge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8 };
    int edgeAt(const QPoint& pos) const;
    void updateCursor(int edges);
    void drag(const QPoint& global);
    void finish();
    void layoutInParent();
    void remember();

    QWidget* m_card;
    QWidget* m_handle;
    QString m_key;
    QSize m_min;
    int m_mode = 0;                 // 0: nothing is held, 1: the card is being moved, 2: it is being resized
    int m_edges = 0;
    QPoint m_press;
    QRect m_start;
    bool m_handlePressed = false;
    bool m_dragged = false;
    bool m_restored = false;
    double m_fx = 0, m_fy = 0;      // where the card is, along the free space of the viewport (0: left / top, 1: right / bottom)
};

}   // namespace FielDes
