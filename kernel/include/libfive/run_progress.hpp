/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "libfive/tree/tree.hpp"

namespace libfive {
namespace run_progress {

/*  What an optimisation looks like while it runs, for a GUI to draw (poll liveView() from any thread).  The optimiser puts a new picture
 *  there after every iteration; the picture of the last one stays until the script ends or the optimisation does (publishLive(nullptr)),
 *  and the script that follows may make others.  The picture is only a picture: nothing is computed from it.  */
/*  A shape of the picture, as a result's shape is: a field that is negative inside it, the fields it can be coloured by (the first is
 *  shown), and the streamlines of a flow -- the viewer draws it by the same code as the result it becomes  */
struct LiveShape
{
    Tree tree = Tree::invalid();
    struct Channel
    {
        std::string name, label;            // (the label is what the colour bar says; the viewer translates it)
        Tree tree = Tree::invalid();
        float lo = 0, hi = 1;
    };
    std::vector<Channel> channels;
    std::vector<std::vector<std::array<float, 5>>> lines;       // streamlines: x y z, the speed there, the time since the seed
    float linesLo = 0, linesHi = 1;         // the speeds the lines are coloured over
    float detail = 0;                       // mm: the colours are read as finely as the solver's elements
    // (a topology picture: the surface of the design itself, welded -- x y z of each vertex, three vertex numbers to a triangle -- which the
    // viewer draws as it is: meshing the field of a part with an imported surface takes seconds, longer than an iteration)
    std::vector<float> surfaceVerts;
    std::vector<uint32_t> surfaceTris;
};

struct LiveView
{
    uint64_t serial = 0;                // set by publishLive: changes with every picture
    std::string kind;                   // what runs: "topology" (the stiffest part for a volume) or "flow" (the body in a flow)
    std::string quantity;               // what `objective` is: "compliance", "drag", or "objective" (drag less lift)
    int iteration = 0, iterations = 0;  // the one just done (counting from 1), and the most there will be
    double objective = 0;               // of the design of this iteration (it should fall)
    double volume = 0;                  // the material of the design, as a fraction of the part's (of the body's own volume, for a flow)
    double volumeLow = 0, volumeHigh = 0;   // what is asked for: a fraction (both the same) or a range
    double change = 0;                  // how far the design moves from this iteration to the next: the largest change of a density
                                        // (topology), the boundary's largest move in mm (flow)
    std::vector<float> history;         // `objective` of every iteration so far
    // The design as it is now, made of the shapes its result is made of (the part cut from the density, the fluid with the body taken out
    // and the body ...): the viewer renders them as it renders the result
    std::vector<LiveShape> shapes;
};
void publishLive(std::shared_ptr<LiveView> view);       // (an empty pointer takes the picture down)
std::shared_ptr<const LiveView> liveView();

/*  How far a script run has got, for a GUI to show (poll it from any
 *  thread).  The script runs step by step: its top-level statements, which
 *  the Python runner reports, so how many there are is known before it
 *  starts.  A long operation inside a step (an FEA solve, an optimisation,
 *  a top-level loop, your own progress() calls) counts how far it is.
 *  Operations one after another in a step each count 0 -> 1 (the detail
 *  says which one runs); one started inside another counts within the
 *  share the outer one gave it (an optimisation iteration holds its solve).
 *
 *  step < 0: no script running.  fraction < 0: nothing in this step has
 *  said how far it is (it hasn't started a long operation).  */
struct State
{
    int step = -1, steps = 0;
    int operation = 0;          // counts the operations begun in the script (a new one: a new bar)
    std::string label;          // the step (its first line of source)
    int line = 0;               // ... and the (1-based) line it starts on, 0 when not told
    double fraction = -1.0;     // of the operation running in this step
    std::string detail;         // what it is doing (outer . inner)
    // Where something counting on its own (a STEP import) goes within the
    // operation's fraction: the innermost operation's share for it
    double spanLo = 0.0, spanHi = 1.0;
};
State current();

/*  A termination asked for by the user (the editor's red dot).  The solvers that run a long time (the static, thermal and flow solves,
 *  the optimisations) look at this flag as they iterate and give up with the message "cancelled"; the script is interrupted by the
 *  interpreter at the same time.  It is cleared when the next run of the script begins.  */
std::atomic<bool>& cancelFlag();
void requestCancel();
void clearCancel();

/*  A pause asked for by the user (the editor's pause / play button).  The script cannot be frozen at any instant (a thread
 *  suspended inside the allocator or while it holds the interpreter's lock would take the whole program with it): it waits at the
 *  next checkpoint, which is every place that reports progress -- a solver's iteration, a step of a STEP import, the start of the
 *  next statement of the script -- and goes on when the pause is lifted (or the run is terminated).  A step that reports nothing
 *  runs to its end first: pausedNow() says whether the script has actually come to a halt.  */
void requestPause(bool on);
bool pauseRequested();
bool pausedNow();
void checkpoint();

// The runner's side
void beginScript(int steps);
void setStep(int index, const std::string& label);
void setStepLine(int line);
void endScript();

class Task
{
public:
    explicit Task(const std::string& name);
    ~Task();
    // How far this operation is, 0..1 (never goes back), and what it does
    void set(double fraction, const std::string& detail = "");
    // The share of this operation an operation started inside it gets
    void span(double a, double b);
private:
    double lo, hi;              // this operation's range of the fraction
    double childLo, childHi;    // an inner operation's range
    Task* parent;
    std::string name, detail;
    friend State current();
};

// The same for callers that can't hold a Task (the C API: Python loops and
// progress()): operations on the calling thread's stack
void pushTask(const std::string& name);
void setTask(double fraction, const std::string& detail);
void spanTask(double a, double b);
void popTask();

}   // namespace run_progress
}   // namespace libfive
