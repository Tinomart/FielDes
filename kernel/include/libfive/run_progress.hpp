/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <string>

namespace libfive {
namespace run_progress {

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
    double fraction = -1.0;     // of the operation running in this step
    std::string detail;         // what it is doing (outer . inner)
    // Where something counting on its own (a STEP import) goes within the
    // operation's fraction: the innermost operation's share for it
    double spanLo = 0.0, spanHi = 1.0;
};
State current();

// The runner's side
void beginScript(int steps);
void setStep(int index, const std::string& label);
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
