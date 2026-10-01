/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QWidget>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTimer>

class QLineEdit;
class QLabel;
class QToolButton;

namespace FielDes {
class Script;

/*
 *  Find / replace bar shown under the script: live highlighting of every
 *  match, case / whole-word / regex options, replace one or all (one undo
 *  step), and "select all matches" which turns every match into a cursor
 *  so they can be edited simultaneously.
 */
class FindBar : public QWidget
{
    Q_OBJECT
public:
    FindBar(Script* script, QWidget* parent=nullptr);

public slots:
    void showFind();
    void showReplace();
    void findNext();
    void findPrevious();
    void replaceOne();
    void replaceAll();
    void selectAllMatches();
    void closeBar();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

    void open(bool replace);
    void updateMatches();
    QRegularExpression pattern(bool* valid) const;
    QString expandReplacement(const QString& matched) const;
    void jumpTo(int index);
    int currentIndex() const;

    Script* m_script;
    QLineEdit* m_find;
    QLineEdit* m_replace;
    QWidget* m_replaceRow;
    QToolButton* m_case;
    QToolButton* m_word;
    QToolButton* m_regex;
    QLabel* m_count;

    QList<QTextCursor> m_matches;
    QTimer m_refresh;
};

}   // namespace FielDes
