/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <functional>

#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QVariantAnimation>

class QAction;
class QFrame;
class QLabel;
class QMainWindow;
class QPushButton;
class QWidget;

namespace FielDes {

class Editor;
class View;
class TourLayer;
class TourBlocker;

/*
 *  The guided tour: it opens the first time FielDes starts, as short cards over the program itself.  Each step says one thing
 *  in a sentence or two, dims everything but what it is about, points at it with an arrow and a glowing frame, and lets the
 *  user do only what the step is about.  Where there is something to see, the tour can do it itself ("Show me"): a cursor moves
 *  over the window and the program answers as it does for the user's own mouse.  Every step can be skipped, and so can the
 *  whole tour, at any time; it can be started again from Help.
 *
 *  It is not a script: it runs on the live program.  It loads a small model of its own (examples/tour.py) to work on.
 */
class Tutorial : public QObject
{
    Q_OBJECT
public:
    /*  `loadTour` puts the tour's model in the editor (false if the user's own script is in the way and they said no)  */
    Tutorial(QMainWindow* window, View* view, Editor* editor, std::function<bool()> loadTour, QObject* parent = nullptr);
    ~Tutorial() override;

    bool active() const { return m_active; }

    /*  The welcome card: start the tour, or not (the first time FielDes runs)  */
    void offer();
    /*  Starts the tour from its first step (loads its model first)  */
    void start();
    /*  Ends it: `completed` is whether it was gone through to the end (the offer is not made again either way)  */
    void stop(bool completed);

    /*  For the tests: where the tour is, and moving it by hand  */
    int stepIndex() const { return m_index; }
    int stepCount() const;
    QString stepTitle() const;
    bool stepDone() const { return m_done; }
    /*  Whether a "Show me" is running, or its result is still being shown (before everything is put back as it was)  */
    bool demoRunning() const { return m_demo; }
    /*  Opens a step.  Every step has a state of its own -- the script, what is selected, whether the section view is on: the first
     *  time it is what the steps before it left, and from then on whenever the step is opened (with Back too) it is put back as it
     *  was when the step was first opened, so that what the step asks for can be done again  */
    void goTo(int index, bool viaBack = false);
    void next();
    void back();
    /*  Shows what the step asks for, once, and then puts everything back as it was before the button was pressed, so that the user
     *  can do it himself  */
    void showMe();

signals:
    void finished(bool completed);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    struct Step;
    enum Place { Auto, Right, Left, Below, Above, Center, InsideTopRight, InsideBottom };

    void buildSteps();
    void showStep();
    void refresh();                         // (where things are now: the target, the card, the arrow)
    void layoutCard(const QRect& hole);
    void updateMask();
    void setCursor(const QPoint& windowPos, bool pressed = false);
    void hideCursor();
    /*  Moves the pretend cursor, sending the mouse events a real hand would  */
    void fakeDrag(QWidget* target, const QPoint& from, const QPoint& to, int ms, std::function<void()> done,
                  Qt::KeyboardModifiers mods = Qt::NoModifier);
    void fakeClick(QWidget* target, const QPoint& at, Qt::MouseButton button, std::function<void()> done);
    void glide(const QPoint& from, const QPoint& to, int ms, std::function<void(QPoint)> each, std::function<void()> done);
    void after(int ms, std::function<void()> fn);
    /*  Types `text` over a range of the script, a character at a time  */
    void typeOver(int line0, int col0, int col1, const QString& text, std::function<void()> done);

    QRect widgetRect(QWidget* w) const;     // (in the window's coordinates)
    QRect scriptLineRect(int line0) const;
    QPoint scriptPoint(int line0, int col) const;
    int scriptLine(const QString& startsWith) const;
    double plateHeight(bool* ok = nullptr) const;
    QWidget* openMenu() const;

    /*  What a step's state is made of (the viewport's camera and the tree's rows are set by the step itself when it is opened)  */
    struct State
    {
        QString script;
        QStringList selected;               // (the variables of the selected models)
        bool section = false;               // (the section view is on)
    };
    State capture() const;
    void restore(const State& state);
    void selectAgain(QStringList names, int tries);
    QAction* sectionAction() const;
    /*  Puts everything back as it was before a "Show me" (at once: the step is changing, or the tour ends)  */
    void endDemo();

    QMainWindow* m_window;
    View* m_view;
    Editor* m_editor;
    std::function<bool()> m_loadTour;
    TourLayer* m_layer = nullptr;
    TourBlocker* m_blocker = nullptr;
    QFrame* m_card = nullptr;
    QLabel* m_counter = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_text = nullptr;
    QLabel* m_note = nullptr;
    QPushButton* m_skipTour = nullptr;
    QPushButton* m_back = nullptr;
    QPushButton* m_show = nullptr;
    QPushButton* m_next = nullptr;

    QVector<Step>* m_steps = nullptr;
    int m_index = -1;
    bool m_active = false;
    bool m_done = false;
    bool m_offering = false;
    bool m_busy = false;                    // (a "Show me" is running)
    bool m_demo = false;                    // (a "Show me" is running or its result is on show: nothing it does counts as the user's work)
    State m_demoBefore;                     // (how things were when the button was pressed)
    QVector<State> m_entry;                 // (the state of each step, as it was when the step was first opened)
    QVector<char> m_hasEntry;
    bool m_viaBack = false;                 // (the step was reached with Back)
    bool m_doneOnArrival = false;           // (what the step asks was done already when it was opened: it does not move on by itself)
    double m_sectionBase = 0;               // (where the section plane was when the step saw it switched on: moving it is the step's task)
    bool m_sectionSeen = false;
    int m_generation = 0;                   // (what a timer of an earlier step must not do)
    double m_baseline = 0;                  // (what a step compares the user's work with)
    QTimer m_poll;
    QTimer m_tick;
    QVector<QRect> m_holes;
    /*  The script as it was last looked at, and the lines of it that were changed or moved by the last edit (they light up for a
     *  moment: what the user does in the viewport or the tree is an edit of these lines)  */
    QStringList m_scriptSeen;
    QVector<int> m_changed;
    QElapsedTimer m_changedClock;
    QString m_revealedFor;                  // (the lines the editor was last scrolled to show)
    void trackChanges();
    QPixmap m_menuPic;                      // (the menu a step is about, as a picture; and where it is drawn)
    QRect m_menuRect;
    QPoint m_cursor;
    bool m_cursorShown = false;
};

}   // namespace FielDes
