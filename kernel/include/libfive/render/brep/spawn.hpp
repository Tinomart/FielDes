/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <future>
#include <thread>
#include <utility>

namespace libfive {

/*  Lowers the priority of the calling thread (a thread of its own, that ends with its work: a priority raised again needs more rights
 *  than a program has on some systems) so that the other threads of the program get the processor first.  Does nothing where that is
 *  not known how to be done */
void lowerThreadPriority();

/*  What std::async(std::launch::async, f) does, with a thread of its own for f.
 *
 *  The standard library of Visual C++ runs an async task on the process's shared thread pool, which starts as many threads as the
 *  machine has cores and adds more slowly (about one for every half second) while all of them are busy.  A render starts nine of them
 *  (its workers and its progress thread) and keeps them until it is done, so a second render started during a long one -- a small
 *  shape changed while a big one was still being meshed -- waited for threads of that pool, and took a hundred times as long as it does
 *  alone.  A thread of one's own starts at once.  Wait for it with the future, as with std::async's. */
template <typename F>
std::future<void> spawn(F&& f)
{
    std::packaged_task<void()> task(std::forward<F>(f));
    auto future = task.get_future();
    std::thread(std::move(task)).detach();
    return future;
}

/*  The same, at a lower priority when `low` is set (see BRepSettings::low_priority) */
template <typename F>
std::future<void> spawn(bool low, F&& f)
{
    std::packaged_task<void()> task(std::forward<F>(f));
    auto future = task.get_future();
    std::thread([low](std::packaged_task<void()> t) {
        if (low) lowerThreadPriority();
        t();
    }, std::move(task)).detach();
    return future;
}

}   // namespace libfive
