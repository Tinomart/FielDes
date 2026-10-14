/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

namespace FielDes {

/*
 *  Where the keyboard is.  The viewport's single keys (W, E, D, R ...) are letters in the script editor, so which pane has the keyboard decides what a key does -- and nothing on
 *  the screen said which one it was.  A FocusCue draws a thin frame round the pane that has it, and says which one it is (`focusedChanged`, for a label in the status bar).
 *  It is a transparent child of the window that is drawn over the panes and lets every mouse event through; the panes are named when they are added, and the keyboard is "in" a pane
 *  when the widget that has it is the pane or one of its children (the model tree is a child of the viewport; the output of the script is a child of the editor).  When the keyboard is
 *  in neither -- a menu, a dialog, another program -- there is no frame.
 */
class FocusCue : public QWidget
{
    Q_OBJECT
public:
    explicit FocusCue(QWidget* window);

    /*  A pane that can have the keyboard; `name` is what `focusedChanged` says when it does  */
    void addPane(QWidget* pane, const QString& name);
    /*  The name of the pane that has the keyboard now, or none  */
    QString focused() const;

signals:
    void focusedChanged(const QString& name);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Pane
    {
        QPointer<QWidget> widget;
        QString name;
    };
    void focusMoved(QWidget* now);

    QVector<Pane> m_panes;
    int m_current = -1;
};

}   // namespace FielDes
