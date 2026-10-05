/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2017-2021  Matt Keeter

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

#include <QObject>

#include "fieldes/result.hpp"
#include "fieldes/documentation.hpp"

namespace FielDes {

class Interpreter : public QObject
{
    Q_OBJECT
public:
    virtual ~Interpreter() { /* Nothing to do here */ }

    /*  Starts up the interpreter */
    virtual void init()=0;

    /*  Returns a reasonable default script */
    virtual QString defaultScript()=0;

    /*  Returns a default extension for this language */
    virtual QString extension()=0;

    /*  Requests that the interpreter stop the current evaluation.
     *  This will be called from the main thread! */
    virtual void halt() {}

    /*  Called from the main thread before the worker thread starts */
    virtual void preinit() {}

    /*  The (1-based) source lines a run stops before: set (from the main
     *  thread) ahead of the eval() that should use them */
    virtual void setBreakpoints(QList<int>) {}

    /*  Asks the language's support code (for Python, fieldes.app_support) a question about the last
     *  run: function(arg) -> text.  Called from the main thread.  On failure the text is empty and
     *  `error` (if given) says why.  */
    virtual QString callSupport(const QString& /*function*/, const QString& /*arg*/, QString* error)
    {
        if (error) *error = "this language cannot answer that";
        return QString();
    }

public slots:
    /*  Evaluates a new script, returning done(...) when done. */
    virtual void eval(QString s)=0;

    /*  Continues a run that stopped at a breakpoint: done(...) again, with
     *  everything so far, when it ends or stops at the next one. */
    virtual void resume() {}

signals:
    void ready(QStringList keywords, Documentation docs);
    void busy();
    void done(Result);

    /*  While a run goes on: the model tree's description of what its finished statements have made so far
     *  (emitted from the run's own helper thread; the run's done() follows with the whole) */
    void partialScene(QString json);
};

} // namespace FielDes
