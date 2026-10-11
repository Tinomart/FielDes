/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2021  Matt Keeter

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#pragma once

#include <algorithm>
#include <atomic>

#include <QMutex>

#include "fieldes/interpreter.hpp"

// Forward declaration of PyObject, because including Python.h wreaks havok
// with Qt headers due to dueling definitions of "slots"
typedef struct _object PyObject;
typedef struct _ts PyThreadState;

namespace FielDes {
namespace Python {

class Interpreter: public ::FielDes::Interpreter {
public:
    Interpreter();
    ~Interpreter();

    void init() override;
    QString defaultScript() override;
    QString extension() override { return ".py"; }

    void halt() override;
    void terminate() override;
    void noteRequest() override;
    void absorbed() override;
    void setRunScript(QString script) override;
    void preinit() override;
    void setBreakpoints(QList<int> lines) override;
    QString callSupport(const QString& function, const QString& arg, QString* error) override;

    /*  What a run has shown so far (the values of its statements and their lines), built into shapes and emitted as partialResult().  Called by
     *  the runner, on the run's own thread, before a statement that takes long  */
    void publishPartial(PyObject* snapshot, PyObject* lines, int runningLine);

public slots:
    void eval(QString s) override;
    void resume() override;

protected:
    // Runs a script (or, resuming, the rest of the one stopped at a
    // breakpoint) and emits done()
    void evaluate(QString script, bool resuming);

    // If the given PyObject represents a shape, record it in out.shapes
    void recordShape(
        PyObject* obj, Result &out, std::map<libfive::Tree::Id, float>& vars,
        int line=-1);

    PyObject* m_runFunc=NULL;
    PyObject* m_resumeFunc=NULL;
    PyObject* m_runnerMod=NULL;
    QMutex m_breakMutex;            // (the lines are set from the main thread)
    QList<int> m_breakpoints;
    QString m_pausedScript;         // the script stopped at a breakpoint
    QString m_runScript;            // the script that is running (an edit that is put into the run replaces it: see setRunScript)
    mutable QMutex m_runScriptMutex;
    QString currentRunScript() const;
    PyObject* m_shapeClass=NULL;
    PyObject* m_varFunc=NULL;
    PyThreadState* m_threadState=NULL;
    unsigned long m_workerThreadId=0;
    std::atomic<bool> m_evaluating{false};  // a script is running in the worker (set and cleared with the GIL held)
    std::atomic<int> m_requests{0};         // the runs of the script that have been asked for and not begun
    QMutex m_queueMutex;                    // (m_requests and m_stale change together)
    int m_stale = 0;                        // of those, the first ones that are of an older text than the run that is going: dropped
    int freshRequests() const { return std::max(0, m_requests.load() - m_stale); }   // (the runs that are still wanted)

};

}   // namespace Python
}   // namespace FielDes
