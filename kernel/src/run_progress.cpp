/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/run_progress.hpp"

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

namespace libfive {
namespace run_progress {

namespace {
std::mutex g_mutex;
State g_state;
Task* g_top = nullptr;      // the innermost operation running

double clamp01(double f) { return std::max(0.0, std::min(1.0, f)); }

// The C API's operations (pushTask ...), per thread
thread_local std::vector<std::unique_ptr<Task>> t_stack;
}

namespace {
// (a pause waits here, not on g_mutex: the program asks for the progress while the script is paused)
std::mutex g_pauseMutex;
std::condition_variable g_pauseCv;
std::atomic<bool> g_pauseOn{false};
std::atomic<int> g_pauseWaiting{0};
}

std::atomic<bool>& cancelFlag()
{
    static std::atomic<bool> flag{false};
    return flag;
}

void requestCancel()
{
    {
        std::lock_guard<std::mutex> lock(g_pauseMutex);
        cancelFlag().store(true);
    }
    g_pauseCv.notify_all();         // (a terminated script that is paused goes on, to end)
}

void clearCancel() { cancelFlag().store(false); }

void requestPause(bool on)
{
    {
        std::lock_guard<std::mutex> lock(g_pauseMutex);
        g_pauseOn.store(on);
    }
    g_pauseCv.notify_all();
}

bool pauseRequested() { return g_pauseOn.load(); }
bool pausedNow() { return g_pauseWaiting.load() > 0; }

void checkpoint()
{
    if (!g_pauseOn.load(std::memory_order_acquire)) return;
    std::unique_lock<std::mutex> lock(g_pauseMutex);
    ++g_pauseWaiting;
    g_pauseCv.wait(lock, [] { return !g_pauseOn.load() || cancelFlag().load(); });
    --g_pauseWaiting;
}

State current()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    State s = g_state;
    if (g_top)
    {
        s.spanLo = g_top->childLo;
        s.spanHi = g_top->childHi;
        // outer . inner (the outermost two say enough)
        std::vector<const Task*> chain;
        for (const Task* t = g_top; t; t = t->parent) chain.push_back(t);
        s.detail.clear();
        for (auto i = chain.rbegin(); i != chain.rend() && i - chain.rbegin() < 2; ++i)
        {
            const std::string& d = (*i)->detail.empty() ? (*i)->name : (*i)->detail;
            if (d.empty()) continue;
            if (!s.detail.empty()) s.detail += " \xc2\xb7 ";
            s.detail += d;
        }
    }
    return s;
}

namespace {
std::shared_ptr<const LiveView> g_live;
uint64_t g_liveSerial = 0;
}

void publishLive(std::shared_ptr<LiveView> view)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (view) view->serial = ++g_liveSerial;
    g_live = view;
}

std::shared_ptr<const LiveView> liveView()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_live;
}

void beginScript(int steps)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_live.reset();                 // (the picture of an earlier run's optimisation)
    g_state = State();
    g_state.step = 0;
    g_state.steps = steps;
}

void setStep(int index, const std::string& label)
{
    checkpoint();       // (the start of a statement: a pause that is asked for between two takes effect here)
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.step = index;
    g_state.label = label;
    g_state.line = 0;
    g_state.fraction = -1.0;
    g_state.detail.clear();
}

void setStepLine(int line)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.line = line;
}

void endScript()
{
    while (!t_stack.empty()) t_stack.pop_back();    // (left open by an error in the script; innermost first)
    std::lock_guard<std::mutex> lock(g_mutex);
    g_live.reset();
    g_state = State();
}

Task::Task(const std::string& n)
    : lo(0.0), hi(1.0), childLo(0.0), childHi(1.0), parent(nullptr), name(n)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    parent = g_top;
    if (parent)
    {
        lo = parent->childLo;
        hi = parent->childHi;
        g_state.fraction = std::max(g_state.fraction, lo);
    }
    else
    {
        g_state.fraction = 0.0;     // (a new operation of this step)
        g_state.operation++;
    }
    childLo = lo;
    childHi = hi;
    g_top = this;
}

Task::~Task()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.fraction = std::max(g_state.fraction, hi);
    if (g_top == this) g_top = parent;
}

void Task::set(double fraction, const std::string& d)
{
    checkpoint();       // (a solver reports its progress at each iteration: where a pause takes effect)
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.fraction = std::max(g_state.fraction, lo + (hi - lo) * clamp01(fraction));
    if (!d.empty()) detail = d;
}

void Task::span(double a, double b)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    childLo = lo + (hi - lo) * clamp01(a);
    childHi = lo + (hi - lo) * clamp01(std::max(a, b));
}

void pushTask(const std::string& name) { t_stack.emplace_back(new Task(name)); }
void setTask(double f, const std::string& d) { if (!t_stack.empty()) t_stack.back()->set(f, d); }
void spanTask(double a, double b) { if (!t_stack.empty()) t_stack.back()->span(a, b); }
void popTask() { if (!t_stack.empty()) t_stack.pop_back(); }

}   // namespace run_progress
}   // namespace libfive
