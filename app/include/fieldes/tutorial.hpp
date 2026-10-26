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
    /*  The document in the editor is the model the tour wrote there, not the user's: no question about saving it is asked while the tour is on, and it is not marked as changed
     *  when the tour ends  */
    bool ownsDocument() const { return m_ownsDocument; }

    /*  The welcome card: start the tour, or not (the first time FielDes runs)  */
    void offer();
    /*  Starts the tour (loads its model first) where it was left -- the step the user last was at, kept in the settings -- or at the first step of the first chapter when
     *  it was never started.  `chapter` >= 0 starts that chapter at its first step instead  */
    void start(int chapter = -1);
    /*  Ends it: `completed` is whether it was gone through to the end (the offer is not made again either way)  */
    void stop(bool completed);

    /*  For the tests: where the tour is, and moving it by hand  */
    int stepIndex() const { return m_index; }
    int stepCount() const;
    QString stepTitle() const;
    /*  The tour is in twelve chapters, one after the other: each ends with a card that congratulates and asks whether to go on or to explore alone.  `chapter()` is the one
     *  of the open step (-1: the welcome card and the card that says where the tour is found again); the first step of each is where the chapter menu of the card, and the
     *  progress that is kept, go to  */
    static int chapterCount();
    int chapter() const;
    int chapterStep(int chapter) const;             // the index of the first step of a chapter
    void goToChapter(int chapter);
    /*  After a chapter: the user explores alone -- the progress is kept at the next chapter, and the card that says where the tour is found again comes  */
    void exploreAlone();
    bool stepDone() const { return m_done; }
    /*  Whether a "Show me" is running, or its result is still being shown (before everything is put back as it was)  */
    bool demoRunning() const { return m_demo; }
    bool demoHolding() const { return m_demoHolding; }
    /*  Opens a step.  Every step has a state of its own -- the script, what is selected, whether the section view is on: the first
     *  time it is what the steps before it left, and from then on whenever the step is opened (with Back too) it is put back as it
     *  was when the step was first opened, so that what the step asks for can be done again  */
    void goTo(int index, bool viaBack = false);
    void next();
    void back();
    /*  Leaves the tour: the card that says where it is found again (Help > Guided tour), and then it is over  */
    void skip();
    /*  Shows what the step asks for, once, and then puts everything back as it was before the button was pressed, so that the user
     *  can do it themselves  */
    void showMe();

signals:
    void finished(bool completed);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    struct Step;
    enum Place { Auto, Right, Left, Below, Above, Center, InsideTopRight, InsideBottom, InsideBottomLeft };

    void buildSteps();
    QString chapterTitle(int chapter) const;
    QString chapterAbout(int chapter) const;        // (what the chapter is about: the description on the card that ends the chapter before it)
    void saveProgress();                            // (the step that is open, and the chapters that are done, in the settings)
    int resumeIndex() const;                        // (the step the tour was left at: the first of the first chapter when it never was)
    void showChapters();                            // (the chapter menu of the card)
    void ensureTourModel(std::function<void()> then);   // (the steps of the first chapters work on the tour's model: it is put in the editor when it is not there)
    bool tourModelHere() const;
    void showStep();
    void showDoneLook(bool done);
    void refresh();                         // (where things are now: the target, the card, the arrow)
    void layoutCard(const QRect& hole, const QVector<QRect>& keepClear);
    void updateMask();
    void setCursor(const QPoint& windowPos, bool pressed = false);
    void hideCursor();
    /*  Moves the pretend cursor, sending the mouse events a real hand would  */
    void fakeDrag(QWidget* target, const QPoint& from, const QPoint& to, int ms, std::function<void()> done,
                  Qt::KeyboardModifiers mods = Qt::NoModifier);
    void fakeClick(QWidget* target, const QPoint& at, Qt::MouseButton button, std::function<void()> done,
                   Qt::KeyboardModifiers mods = Qt::NoModifier);
    void glide(const QPoint& from, const QPoint& to, int ms, std::function<void(QPoint)> each, std::function<void()> done);
    void after(int ms, std::function<void()> fn);
    /*  Types `text` over a range of the script, a character at a time  */
    void typeOver(int line0, int col0, int col1, const QString& text, std::function<void()> done, int msPerCharacter = 120);

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
        QString symmetry;                   // (the symmetry in use: the variable of the model, or none)
    };
    State capture() const;
    void restore(const State& state);
    /*  Puts a whole script in the editor (a step that works on a model of its own), without it counting as a change of the user's  */
    void useScript(const QString& text, bool frame = true);       // (`frame`: the camera goes to the region the script sets, at once)
    void selectAgain(QStringList names, int tries, int epoch = -1);
    void symmetryAgain(const QString& var, int tries);         // (the symmetry in use is put back once its model is in the tree)
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
    QPushButton* m_chapters = nullptr;
    int m_modelTries = 0;                   // (how many times the tour's model was put in the editor for the step that is being opened: it is not asked for again and again)
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
    State m_groupBefore;                    // (the state before the first step of a group that works on models of its own: put back when the group is left)
    bool m_hasGroup = false;
    QString m_stepScript;                   // (the script as it was when the step was opened: a step whose work is its own undoes it)
    QVector<State> m_entry;                 // (the state of each step, as it was when the step was first opened)
    QVector<char> m_hasEntry;
    bool m_viaBack = false;                 // (the step was reached with Back)
    bool m_doneOnArrival = false;           // (what the step asks was done already when it was opened: it does not move on by itself)
    double m_sectionBase = 0;               // (where the section plane was when the step saw it switched on: moving it is the step's task)
    bool m_sectionSeen = false;
    int m_generation = 0;                   // (what a timer of an earlier step must not do)
    int m_selectEpoch = 0;                  // (a selection that is asked for replaces the ones asked for before it: a late retry of the older one must not select over it)
    double m_baseline = 0;                  // (what a step compares the user's work with)
    QString m_baselineText;                 // (the same, when the work is a stretch of the script)
    bool m_viewTouched = false;             // (the user has pressed in the view or turned the wheel since a step put its own model there: its camera is theirs)
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
    bool m_ownsDocument = false;            // (the tour's model is the document in the editor: see ownsDocument)
    bool m_menuTriggered = false;           // (the entry of the menu the step is about has been chosen, or the menu it holds has opened)
    QVector<QMetaObject::Connection> m_menuConns;    // (what tells that: connected when the step opens, undone when it changes)
    bool m_cardMoved = false;               // (the user dragged the card: it stays where they put it until the step changes)
    bool m_demoHolding = false;             // (a "Show me" has made what it shows and it is on the screen for a moment before everything is put back)
    int m_busyTicks = 0;                    // (how long, in polls, the script has been running or the viewport drawing: past a second the card says it is working)
    bool m_noteBusy = false;                // (the line under the card's text says that now)
    bool m_dragging = false;
    QPoint m_dragPress, m_dragStart;        // (where the mouse was pressed, on the screen, and where the card was)
    int m_popState = 0;                     // (the step about pulling a pane out: 0 not yet, 1 the viewport has been out)
};

}   // namespace FielDes
