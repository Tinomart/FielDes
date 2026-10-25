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
#include "fieldes/language.hpp"
#include "libfive/run_progress.hpp"

namespace FielDes {

Language::Language(Interpreter* interpreter,
                   Formatter* formatter,
                   Syntax* syntax, Type type)
    : m_interpreter(interpreter), m_formatter(formatter), m_syntax(syntax),
      m_type(type), m_showDocsWhenReady(false)
{
    // Run halt() in the main thread when the script changes, then pass data
    // into the interpreter worker thread
    connect(this, &Language::onScriptChanged,
            this, [=](QString text){
                // The run that is going is asked what the edit does to it (fieldes.app_support.edit_in_place).  An edit that changes nothing of
                // what the statement that runs is made of or makes -- a model shown or hidden, whatever comes after it -- is put into the run:
                // it is not stopped, a calculation of minutes goes on, and no run follows.  One at or above what that statement is made of
                // changes what it makes: a solver that works on it is stopped now, not waited for.  One in a text that is being typed, below
                // it, lets the statement end (its result is kept for the next run) and then makes way
                m_interpreter->noteRequest();
                QString error;
                const QString effect = m_interpreter->callSupport("edit_in_place", text, &error);
                if (effect == "inplace")
                {
                    // (the runs that edits before this one asked for, and that wait, are of an older text now: see Interpreter::absorbed)
                    m_interpreter->setRunScript(text);
                    m_interpreter->absorbed();
                    return;
                }
                if (effect == "reaches")
                    libfive::run_progress::requestCancel();
                if (effect != "defer")
                    m_interpreter->halt();
                emit(evalRequested(text));
            });
    connect(this, &Language::evalRequested,
            m_interpreter.data(), &Interpreter::eval);

    connect(this, &Language::onResume,
            m_interpreter.data(), &Interpreter::resume);

    // Signals coming out of the interpreter are attached below:
    connect(m_interpreter.data(), &Interpreter::ready,
            this, &Language::onInterpreterReady);
    connect(m_interpreter.data(), &Interpreter::done,
            this, &Language::interpreterDone);
    connect(m_interpreter.data(), &Interpreter::busy,
            this, &Language::interpreterBusy);
    connect(m_interpreter.data(), &Interpreter::partialScene,
            this, &Language::interpreterPartialScene);
    connect(m_interpreter.data(), &Interpreter::partialResult,
            this, &Language::interpreterPartialResult);

    connect(this, &Language::onCursorMoved,
            m_syntax.data(), &Syntax::onCursorMoved);

    // Kick off the interpreter thread, which causes init() to run
    connect(&m_interpreterThread, &QThread::started,
            m_interpreter.data(), &Interpreter::init);
    m_interpreter->preinit();
    m_interpreter->moveToThread(&m_interpreterThread);
    m_interpreterThread.start();
}

Language::~Language() {
    // (a solver that is going is told first: halt() reaches the script only when the call it is in comes back)
    libfive::run_progress::requestCancel();
    m_interpreter->halt();
    m_interpreterThread.quit();
    m_interpreterThread.wait();
}

void Language::onShowDocs() {
    if (m_docs) {
        m_docs->show();
    } else {
        m_showDocsWhenReady = true;
    }
}

QString Language::defaultScript() {
    return m_interpreter->defaultScript();
}

QString Language::extension() {
    return m_interpreter->extension();
}

Language::Type Language::type() {
    return m_type;
}

void Language::onInterpreterReady(QStringList keywords, Documentation docs) {
    m_syntax->setKeywords(keywords);
    emit(syntaxReady());
    emit(languageReady(keywords, docs));

    m_docs.reset(new DocumentationPane(docs));
    if (m_showDocsWhenReady) {
        m_docs->show();
    }
}

}   // namespace FielDes
