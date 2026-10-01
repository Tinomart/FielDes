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
#include <Python.h>
#include <QApplication>
#include <QMessageBox>
#include <QRegularExpression>
#include <cstdlib>
#include <cstring>

#include "fieldes/python/interpreter.hpp"
#include "fieldes/documentation.hpp"
#include "fieldes/shape.hpp"

#include "libfive.h"

// We inject a dummy module named "_fieldes_host" into the Python namespace (fieldes.view
// re-exports its functions), then
// use it to control settings (resolution, bounds, etc)
static PyObject* set_resolution(PyObject* self, PyObject* args) {
    double res;
    if (!PyArg_ParseTuple(args, "d", &res)) {
        return NULL;
    }
    PyObject_SetAttrString(self, "__resolution", PyFloat_FromDouble(res));
    Py_RETURN_NONE;
}

static PyObject* set_quality(PyObject* self, PyObject* args) {
    double qua;
    if (!PyArg_ParseTuple(args, "d", &qua)) {
        return NULL;
    }
    PyObject_SetAttrString(self, "__quality", PyFloat_FromDouble(qua));
    Py_RETURN_NONE;
}

static PyObject* set_bounds(PyObject* self, PyObject* args) {
    double xmin, ymin, zmin, xmax, ymax, zmax;
    if (!PyArg_ParseTuple(args, "(ddd)(ddd)", &xmin, &ymin, &zmin,
                          &xmax, &ymax, &zmax))
    {
        // The error flag is set by PyArg_ParseTuple
        return NULL;
    }
    PyObject_SetAttrString(self, "__bounds", args);
    Py_RETURN_NONE;
}

static PyObject* var_func(PyObject* host_mod, PyObject* args) {
    if (PyTuple_Size(args) == 1) {
        const char* err_str;
        if (!PyArg_ParseTuple(args, "s", &err_str)) {
            return NULL;
        } else {
            PyErr_SetString(PyExc_RuntimeError, err_str);
            return NULL;
        }
    }
    // Parse the argument, which should be patched in the AST by runner.run
    double value;
    int lineno, end_lineno, col_offset, end_col_offset;
    if (!PyArg_ParseTuple(args, "d(iiii)", &value,
        &lineno, &end_lineno, &col_offset, &end_col_offset))
    {
            return NULL;
    }

    const auto prev_vars = PyObject_GetAttrString(host_mod, "__prev_vars");
    const auto vars = PyObject_GetAttrString(host_mod, "__vars");
    const auto num_vars = PyList_Size(vars);
    PyErr_Print();

    PyObject* v;
    if (num_vars < PyList_Size(prev_vars)) {
        // Pull just the Shape object from the tuple
        auto item = PyList_GetItem(prev_vars, num_vars); // borrowed
        v = PyTuple_GetItem(item, 0); // borrowed
        Py_INCREF(v);
    } else {
        // Build a new free variable using Shape.var()
        const auto shape_mod = PyImport_ImportModule("fieldes.shape");
        const auto Shape = PyObject_GetAttrString(shape_mod, "Shape");
        v = PyObject_CallMethod(Shape, "var", NULL); // new reference
        Py_DECREF(shape_mod);
        Py_DECREF(Shape);
    }

    const auto new_var = Py_BuildValue("Od(iiii)",
        v, value, lineno, end_lineno, col_offset, end_col_offset);
    PyList_Append(vars, new_var);

    Py_DECREF(new_var);
    Py_DECREF(vars);
    Py_DECREF(prev_vars);
    PyErr_Print();

    return v;
}

static PyMethodDef host_methods[] = {
    {"set_resolution", set_resolution, METH_VARARGS, "Sets render resolution"},
    {"set_quality", set_quality, METH_VARARGS, "Sets render quality"},
    {"set_bounds", set_bounds, METH_VARARGS, "Sets render bounds"},
    {"__var", var_func, METH_VARARGS, "Constructs a free variable"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef host_module = {
    PyModuleDef_HEAD_INIT,
    "_fieldes_host",   /* name of module */
    "Global parameters to control the FielDes viewport",
    -1,
    host_methods, // m_methods
    NULL, // m_slots
    NULL, // m_traverse
    NULL, // m_clear
    NULL, // m_free
};

PyMODINIT_FUNC PyInit_fieldes_host(void) {
    return PyModule_Create(&host_module);
}

////////////////////////////////////////////////////////////////////////////////

namespace FielDes {
namespace Python {

const static QString SET_QUALITY_STR = "view.set_quality(%1)\n";
const static QString SET_RESOLUTION_STR = "view.set_resolution(%1)\n";
const static QString SET_BOUNDS_STR = "view.set_bounds([%1, %2, %3], "
                                                        "[%4, %5, %6])\n";

Interpreter::Interpreter() {
    // Nothing to do here
}

Interpreter::~Interpreter() {
    // The destructor is called from the main thread after the interpreter
    // thread has finished.  We reclaim the thread state, so the GIL lives
    // in the main thread until another Interpreter is created.
    PyEval_RestoreThread(m_threadState);

    Py_XDECREF(m_runFunc);
    Py_XDECREF(m_resumeFunc);
    Py_XDECREF(m_shapeClass);
    Py_XDECREF(m_varFunc);
}

QString Interpreter::defaultScript() {
    auto default_settings = Settings::defaultSettings();
    return "from fieldes import *\n\n" +
        SET_BOUNDS_STR.arg(default_settings.min.x())
                      .arg(default_settings.min.y())
                      .arg(default_settings.min.z())
                      .arg(default_settings.max.x())
                      .arg(default_settings.max.y())
                      .arg(default_settings.max.z()) +
        SET_QUALITY_STR.arg(default_settings.quality) +
        SET_RESOLUTION_STR.arg(default_settings.res) +
        "\nsphere(1)";
}

void Interpreter::halt() {
    // Restore the thread state and claim the GIL
    PyEval_RestoreThread(m_threadState);

    // Interrupt the worker thread by ID
    PyThreadState_SetAsyncExc(m_workerThreadId, PyExc_Exception);

    // Then release the thread state again, saving it locally
    m_threadState = PyEval_SaveThread();
}

void Interpreter::preinit() {
    if (!Py_IsInitialized()) {
        QDir app_dir;

#ifdef Q_OS_WIN
        // On Windows, we also write the PYTHONHOME environment variable, so
        // that Python can find itself.  We assume that things were built per
        // the README, i.e. a directory named vcpkg above the build directory
        // (although this will also catch a global vcpkg in C:\vcpkg)
        const QStringList homes = {QDir::toNativeSeparators("runtime/python3"),
                                   QDir::toNativeSeparators("vcpkg/installed/x64-windows/tools/python3")};
        app_dir = QDir(QCoreApplication::applicationDirPath());
        bool found = false;
        do {
            for (const auto& home_dir : homes) {
                if (app_dir.exists(home_dir)) {
                    qputenv("PYTHONHOME", app_dir.filePath(home_dir).toLocal8Bit());
                    found = true;
                    break;
                }
            }
        } while (!found && app_dir.cdUp());
#endif

        PyImport_AppendInittab("_fieldes_host", PyInit_fieldes_host);
        Py_Initialize();

        // Walk up directories from the program, looking for the folder the `fieldes`
        // package lives in (python/ in the repository and in a packaged copy), and
        // add it to sys.path
        const auto bind_dir = QDir::toNativeSeparators("python/fieldes");
        app_dir = QDir(QCoreApplication::applicationDirPath());
        do {
            if (app_dir.exists(bind_dir)) {
                const auto s = PyUnicode_FromString(
                    app_dir.filePath("python").toLocal8Bit().data());
                PyList_Insert(PySys_GetObject("path"), 1, s);
                Py_DECREF(s);
                break;
            }
        } while (app_dir.cdUp());

        // Create a dummy module for settings
        PyModule_Create(&host_module);

        // A copy of the folder without its Python library cannot work: say so here, rather than
        // crashing when the first script runs
        const auto library = PyImport_ImportModule("fieldes.runner");
        if (!library) {
            PyErr_Print();
            QMessageBox::critical(nullptr, "FielDes",
                "FielDes cannot load its Python library: the folder python\\fieldes and fieldes.dll "
                "must stay next to FielDes.exe, as in the downloaded folder.\n\n"
                "Details are on the console (start FielDes.exe from a terminal).");
            std::exit(1);
        }
        Py_DECREF(library);
    }
    m_threadState = PyEval_SaveThread();
}

void Interpreter::init() {
    PyGILState_STATE gstate = PyGILState_Ensure();

    // TODO: do a first import check and raise a reasonable error instead of
    // crashing if the module isn't available? (it *should* always be available)

    const auto runner_mod = PyImport_ImportModule("fieldes.runner");
    PyErr_Print();
    m_runFunc = PyObject_GetAttrString(runner_mod, "run");
    m_resumeFunc = PyObject_GetAttrString(runner_mod, "resume");
    PyErr_Clear();
    m_runnerMod = runner_mod;   // kept: run() leaves scene info on it

    const auto shape_mod = PyImport_ImportModule("fieldes.shape");
    PyErr_Print();
    m_shapeClass = PyObject_GetAttrString(shape_mod, "Shape");
    Py_DECREF(shape_mod);

    const auto threading_mod = PyImport_ImportModule("threading");
    const auto get_ident = PyObject_GetAttrString(threading_mod, "get_ident");
    const auto thread_id = PyObject_CallFunctionObjArgs(get_ident, NULL);
    m_workerThreadId = PyLong_AsLong(thread_id);
    Py_DECREF(thread_id);
    Py_DECREF(get_ident);
    Py_DECREF(threading_mod);

    // Store the var() function locally for easy access
    const auto host_mod = PyImport_ImportModule("_fieldes_host");
    m_varFunc = PyObject_GetAttrString(host_mod, "__var");

    // Reset studio.__vars to an empty list
    const auto empty_list = PyList_New(0);
    PyObject_SetAttrString(host_mod, "__vars", empty_list);
    Py_DECREF(empty_list);
    Py_DECREF(host_mod);

    QStringList keywords;

    // Equivalent to str(obj), returning a QString
    auto str = [](PyObject* obj) -> QString {
        const auto s = PyObject_Str(obj);
        const auto ws = PyUnicode_AsWideCharString(s, NULL);
        const auto out = QString::fromWCharArray(ws);
        PyMem_Free(ws);
        Py_DECREF(s);
        return out;
    };
    auto dump_keywords = [&](PyObject* list) {
        const auto list_len = PyList_Size(list);
        for (unsigned i=0; i < list_len; ++i) {
            keywords << str(PyList_GetItem(list, i));
        }
    };

    {   // Highlight all Python keywords
        const auto keyword_mod = PyImport_ImportModule("keyword");
        const auto kwlist = PyObject_GetAttrString(keyword_mod, "kwlist");
        dump_keywords(kwlist);
        Py_DECREF(kwlist);
        Py_DECREF(keyword_mod);
    }

    {   // Highlight all Python builtins
        const auto builtins_mod = PyImport_ImportModule("builtins");
        const auto builtins_list = PyObject_Dir(builtins_mod);
        dump_keywords(builtins_list);
        Py_DECREF(builtins_list);
        Py_DECREF(builtins_mod);
    }

    Documentation docs;

    {   // Load all the keywords from the stdlib, then build the docs
        const auto stdlib_mod = PyImport_ImportModule("fieldes.stdlib");
        const auto stdlib_list = PyObject_Dir(stdlib_mod);
        const auto stdlib_len = PyList_Size(stdlib_list);
        PyErr_Print();

        const auto inspect_mod = PyImport_ImportModule("inspect");
        const auto sig = PyObject_GetAttrString(inspect_mod, "signature");
        const auto getdoc = PyObject_GetAttrString(inspect_mod, "getdoc");
        const auto getmod = PyObject_GetAttrString(inspect_mod, "getmodule");
        PyErr_Print();

        for (unsigned i=0; i < stdlib_len; ++i) {
            const auto item = PyList_GetItem(stdlib_list, i); // borrowed ref
            const auto item_str = str(item);
            if (item_str.startsWith("_")) {
                continue;
            }
            keywords << item_str;

            // Don't add classes to the standard library documentation
            const auto item_obj = PyObject_GetAttr(stdlib_mod, item);
            if (!PyFunction_Check(item_obj)) {
                Py_DECREF(item_obj);
                continue;
            }
            const auto item_name_obj = PyObject_GetAttrString(
                    item_obj, "__name__");
            const auto item_name = str(item_name_obj);
            Py_DECREF(item_name_obj);

            const auto item_signature = PyObject_CallFunctionObjArgs(
                    sig, item_obj, NULL);
            const auto item_docstring = PyObject_CallFunctionObjArgs(
                    getdoc, item_obj, NULL);

            if (!item_signature || !item_docstring) {
                std::cerr << "Warning: missing documentation for "
                    << item_str.toStdString()
                    << "; " << str(item_signature).toStdString()
                    << "; " << str(item_docstring).toStdString();
            }
            const auto mod = PyObject_CallFunctionObjArgs(
                    getmod, item_obj, NULL);
            const auto mod_name = PyObject_GetAttrString(mod, "__name__");
            const auto mod_name_str = str(mod_name);
            Py_DECREF(mod_name);
            Py_DECREF(mod);

            docs[mod_name_str][item_str] =
                (item_name + str(item_signature) + "\n" +
                 str(item_docstring)).trimmed();

            Py_DECREF(item_obj);
            Py_XDECREF(item_signature);
            Py_XDECREF(item_docstring);
        }

        Py_DECREF(getdoc);
        Py_DECREF(getmod);
        Py_DECREF(sig);
        Py_DECREF(inspect_mod);
        Py_DECREF(stdlib_list);
        Py_DECREF(stdlib_mod);
    }

    {   // Editor support: definition locations, member names and call tips,
        // passed along in reserved "__" sections of the documentation map
        // (the documentation pane skips those).
        const auto support_mod = PyImport_ImportModule("fieldes.app_support");
        if (support_mod) {
            const auto info = PyObject_CallMethod(support_mod, "completion_info", NULL);
            if (info && PyList_Check(info)) {
                const auto n = PyList_Size(info);
                for (Py_ssize_t i=0; i < n; ++i) {
                    const auto rec = str(PyList_GetItem(info, i)).split('\t');
                    if (rec.size() == 4 && rec[0] == "def") {
                        docs["__defs__"][rec[1]] = rec[2] + "\t" + rec[3];
                    } else if (rec.size() == 3 && rec[0] == "member") {
                        auto& m = docs["__members__"][rec[1]];
                        m = m.isEmpty() ? rec[2] : m + " " + rec[2];
                    } else if (rec.size() == 3 && rec[0] == "tip") {
                        docs["__tips__"][rec[1]] = rec[2];
                    }
                }
            }
            Py_XDECREF(info);
            Py_DECREF(support_mod);
        }
        PyErr_Print();
    }

    PyErr_Print();

    PyGILState_Release(gstate);
    emit(ready(keywords, docs));
}

// Formats whatever Python exception is currently set (via PyErr_Fetch/
// PyErr_Restore, exactly like CPython's own default excepthook) into a
// QString, the same way a script-level exception has always been reported.
// Must be called with an exception set; clears it as a side effect (same
// as the printing this replaces did).
static QString capturePythonTraceback()
{
    PyObject *type, *value, *traceback;
    PyErr_Fetch(&type, &value, &traceback);

    auto io_mod = PyImport_ImportModule("io");
    auto string_io = PyObject_GetAttrString(io_mod, "StringIO");
    auto new_stderr = PyObject_CallObject(string_io, NULL);
    PySys_SetObject("stderr", new_stderr);

    PyErr_Restore(type, value, traceback);
    PyErr_Print();

    const auto s = PyObject_CallMethod(new_stderr, "getvalue", NULL);
    const auto ws = PyUnicode_AsWideCharString(s, NULL);
    QString out = QString::fromWCharArray(ws);

    PySys_SetObject("stderr", PySys_GetObject("__stderr__"));

    PyMem_Free(ws);
    Py_DECREF(s);
    Py_DECREF(new_stderr);
    Py_DECREF(string_io);
    Py_DECREF(io_mod);

    // Drop the runner's own frames ("File .../runner.py ... in run" plus
    // its source line): they're FielDes internals, not the user's code
    QStringList lines = out.split('\n');
    QStringList kept;
    for (int i=0; i < lines.size(); ++i)
    {
        if (lines[i].trimmed().startsWith("File ") && lines[i].contains("runner.py"))
        {
            if (i + 1 < lines.size() && !lines[i + 1].trimmed().startsWith("File "))
            {
                ++i;    // skip the source line shown under the frame
            }
            continue;
        }
        kept << lines[i];
    }
    return kept.join('\n');
}

// Finds the script line an error refers to: the last frame in the user's
// script ('File "<file>", line N') or, for a syntax error, the parser's
// report ('File "<unknown>", line N').  Returns a zero-width range on that
// (0-based) line, meaning "the whole line", or a null QRect if none.
static QRect errorRangeFromTraceback(const QString& tb)
{
    static const QRegularExpression frame(
        R"re(File "<(?:file|unknown)>", line (\d+))re");
    int line = -1;
    auto it = frame.globalMatch(tb);
    while (it.hasNext())
    {
        line = it.next().captured(1).toInt();
    }
    return (line > 0) ? QRect(0, line - 1, 0, 1) : QRect();
}

void Interpreter::setBreakpoints(QList<int> lines)
{
    QMutexLocker lock(&m_breakMutex);
    m_breakpoints = lines;
}

void Interpreter::eval(QString script)
{
    evaluate(script, false);
}

void Interpreter::resume()
{
    evaluate(m_pausedScript, true);
}

void Interpreter::evaluate(QString script, bool resuming)
{
    emit(busy());

    PyGILState_STATE gstate = PyGILState_Ensure();

    const auto host_mod = PyImport_ImportModule("_fieldes_host");

    // The lines to stop before, as a Python list
    QList<int> breakpoints;
    {
        QMutexLocker lock(&m_breakMutex);
        breakpoints = m_breakpoints;
    }
    const auto breaks = PyList_New(0);
    for (const int b : breakpoints)
    {
        const auto n = PyLong_FromLong(b);
        PyList_Append(breaks, n);
        Py_DECREF(n);
    }

    PyObject* vars_list;
    PyObject* ret;
    if (!resuming)
    {
        // Reset all settings
        PyObject_SetAttrString(host_mod, "__resolution", Py_None);
        PyObject_SetAttrString(host_mod, "__quality", Py_None);
        PyObject_SetAttrString(host_mod, "__bounds", Py_None);

        // Copy from __vars to __prev_vars, then reset __vars
        const auto prev_vars = PyObject_GetAttrString(host_mod, "__vars");
        PyObject_SetAttrString(host_mod, "__prev_vars", prev_vars);
        Py_DECREF(prev_vars);
        vars_list = PyList_New(0);
        PyObject_SetAttrString(host_mod, "__vars", vars_list);

        auto args = PyTuple_New(1);
        // UTF-8, not the local 8-bit code page: PyUnicode_FromString decodes
        // UTF-8, so any non-ASCII character (an accented path, a dash in a
        // comment) used to be mangled or rejected
        PyTuple_SetItem(args, 0,
                PyUnicode_FromString(script.toUtf8().data()));
        auto kwargs = PyDict_New();
        PyDict_SetItemString(kwargs, "var", m_varFunc);
        PyDict_SetItemString(kwargs, "breakpoints", breaks);
        ret = PyObject_Call(m_runFunc, args, kwargs);
        Py_DECREF(args);
        Py_DECREF(kwargs);
    }
    else
    {
        // The rest of the stopped script: its settings and variables so far
        // are kept
        vars_list = PyObject_GetAttrString(host_mod, "__vars");
        auto args = PyTuple_New(1);
        Py_INCREF(breaks);
        PyTuple_SetItem(args, 0, breaks);
        ret = m_resumeFunc ? PyObject_Call(m_resumeFunc, args, NULL) : NULL;
        if (!ret && !PyErr_Occurred())
        {
            PyErr_SetString(PyExc_RuntimeError, "cannot continue: no script is stopped");
        }
        Py_DECREF(args);
    }
    Py_DECREF(breaks);

    Result out;
    out.settings = Settings::defaultSettings();
    out.okay = ret && !PyErr_Occurred();

    // Map storing vars and their desired values (with the mapping to positions
    // stored elsewhere in the Result structure)
    std::map<libfive::Tree::Id, float> vars;

    if (out.okay) {
        // Did it stop at a breakpoint (runner.last_paused, a line)?
        m_pausedScript.clear();
        if (m_runnerMod) {
            const auto paused = PyObject_GetAttrString(m_runnerMod, "last_paused");
            if (paused && PyLong_Check(paused) && PyLong_AsLong(paused) > 0) {
                out.pausedLine = int(PyLong_AsLong(paused));
                m_pausedScript = script;
            }
            Py_XDECREF(paused);
            PyErr_Clear();
        }

        // Parse vars from studio.__vars
        const auto vars_size = PyList_Size(vars_list);
        for (unsigned i=0; i < vars_size; ++i) {
            PyObject* s;
            double value;
            int lineno, end_lineno, col_offset, end_col_offset;
            const auto item = PyList_GetItem(vars_list, i);

            PyArg_ParseTuple(item, "Od(iiii)", &s, &value,
                &lineno, &end_lineno, &col_offset, &end_col_offset);
            PyErr_Print();

            // Link up the ID with the value and position in the text
            const auto ptr_obj = PyObject_GetAttrString(s, "ptr");
            PyErr_Print();
            const auto ptr = PyLong_AsVoidPtr(ptr_obj);
            PyErr_Print();
            const auto id = static_cast<libfive::Tree::Id>(ptr);
            vars[id] = value;
            out.vars[id] = QRect(col_offset, lineno - 1,
                                 end_col_offset - col_offset,
                                 end_lineno - lineno);
            Py_DECREF(ptr_obj);
            PyErr_Print();
        }

        // Evaluating the script's last expression (e.g. a bare `shape`)
        // never itself raises -- it's just a name lookup -- so a value
        // whose __repr__ raises (e.g. a STEP import's FailedPart, standing
        // in for a solid that couldn't be reconstructed) only surfaces an
        // exception HERE, formatting it for the status line. Previously
        // unchecked: PyObject_Repr returning NULL on such a value fed NULL
        // straight into PyUnicode_AsWideCharString, an instant crash --
        // confirmed by a real STEP file with a genuinely non-analytic
        // solid. Treat it exactly like any other script-level exception
        // instead: report it in the GUI, don't crash, don't render.
        bool reprOk = true;
        const auto ret_size = PyList_Size(ret);
        if (ret_size) {
            const auto s = PyObject_Repr(PyList_GetItem(ret, ret_size - 1));
            if (!s) {
                out.okay = false;
                const auto tb = capturePythonTraceback();
                out.error = {tb, errorRangeFromTraceback(tb)};
                out.result = "None";
                reprOk = false;
            } else {
                const auto ws = PyUnicode_AsWideCharString(s, NULL);
                out.result = QString::fromWCharArray(ws);
                PyMem_Free(ws);
                Py_DECREF(s);
                // A shape, or a list of shapes, is drawn in the viewport: its repr would only be noise
                if (out.result.contains("<shape@"))
                {
                    QString rest = out.result;
                    rest.remove(QRegularExpression("<shape@0x[0-9a-fA-F]+>"));
                    rest.remove(QRegularExpression("[\\[\\]\\(\\),\\s]"));
                    if (rest.isEmpty()) out.result = "None";
                }
            }
        } else {
            out.result = "None";
        }
        // What the script printed goes first (the value of its last line
        // after it, unless that is None)
        if (m_runnerMod) {
            const auto printed = PyObject_GetAttrString(m_runnerMod, "last_output");
            if (printed && PyUnicode_Check(printed)) {
                QString text = QString::fromUtf8(PyUnicode_AsUTF8(printed));
                while (text.endsWith('\n')) text.chop(1);
                if (!text.isEmpty()) {
                    out.result = (out.result == "None") ? text : text + "\n" + out.result;
                }
            }
            Py_XDECREF(printed);
            PyErr_Clear();
        }
        // Source line of each top-level statement (runner.last_lines), so
        // every rendered shape knows which statement displayed it
        QVector<int> stmt_lines;
        if (m_runnerMod) {
            const auto lines = PyObject_GetAttrString(m_runnerMod, "last_lines");
            if (lines && PyList_Check(lines)) {
                const auto n = PyList_Size(lines);
                for (Py_ssize_t k=0; k < n; ++k) {
                    const auto t = PyList_GetItem(lines, k);
                    stmt_lines << (PyTuple_Check(t)
                        ? int(PyLong_AsLong(PyTuple_GetItem(t, 0))) - 1 : -1);
                }
            }
            Py_XDECREF(lines);
            const auto scene = PyObject_GetAttrString(m_runnerMod, "last_scene");
            if (scene && PyUnicode_Check(scene)) {
                out.scene = QString::fromUtf8(PyUnicode_AsUTF8(scene));
            }
            Py_XDECREF(scene);
            PyErr_Clear();
        }

        for (unsigned i=0; reprOk && i < ret_size; ++i) {
            const auto s = PyList_GetItem(ret, i);
            const int line = (int(i) < stmt_lines.size()) ? stmt_lines[i] : -1;
            recordShape(s, out, vars, line);

            const auto iter = PyObject_GetIter(s);
            if (iter) {
                PyObject* item = NULL;
                while ((item = PyIter_Next(iter))) {
                    recordShape(item, out, vars, line);
                    Py_DECREF(item);
                }
                Py_DECREF(iter);
            } else {
                // PyObject_GetIter sets an error
                PyErr_Clear();
            }
        }

      if (reprOk) {
        // Check whether settings were assigned in the script
        auto res = PyObject_GetAttrString(host_mod, "__resolution");
        if (res != Py_None) {
            const double r = PyFloat_AsDouble(res);
            if (!PyErr_Occurred()) {
                out.settings.res = r;
            } else {
                res = NULL;
            }
        }
        if (res == Py_None || res == NULL) {
            out.warnings.append({
                    "<b>Warning:</b> no <code>view.set_resolution(...)</code>: using the default",
                    SET_RESOLUTION_STR.arg(out.settings.res)});
        }
        Py_XDECREF(res);

        auto qua = PyObject_GetAttrString(host_mod, "__quality");
        if (qua != Py_None) {
            const double q = PyFloat_AsDouble(qua);
            if (!PyErr_Occurred()) {
                out.settings.quality = q;
            } else {
                qua = NULL;
            }
        }
        if (qua == Py_None || qua == NULL) {
            out.warnings.append({
                    "<b>Warning:</b> no <code>view.set_quality(...)</code>: using the default",
                    SET_QUALITY_STR.arg(out.settings.quality)});
        }
        Py_XDECREF(qua);

        auto bounds = PyObject_GetAttrString(host_mod, "__bounds");
        if (bounds != Py_None) {
            double xmin, ymin, zmin, xmax, ymax, zmax;
            if (PyArg_ParseTuple(bounds, "(ddd)(ddd)", &xmin, &ymin, &zmin,
                                  &xmax, &ymax, &zmax))
            {
                out.settings.min = QVector3D(xmin, ymin, zmin);
                out.settings.max = QVector3D(xmax, ymax, zmax);
            } else {
                PyErr_Clear();
                bounds = NULL;
            }
        }
        if (bounds == Py_None || bounds == NULL) {
            out.warnings.append(
                    {"<b>Warning:</b> no <code>view.set_bounds(...)</code>: using the default",
                    SET_BOUNDS_STR.arg(out.settings.min.x())
                                  .arg(out.settings.min.y())
                                  .arg(out.settings.min.z())
                                  .arg(out.settings.max.x())
                                  .arg(out.settings.max.y())
                                  .arg(out.settings.max.z())});
        }
        Py_XDECREF(bounds);

        // A resolution far finer than the region needs (e.g. 1000 per unit
        // kept from when a model was imported in metres, now in mm: a 1 um
        // voxel across a table) makes the mesher run practically forever,
        // and every edit re-runs the script next to it
        {
            const QVector3D ext = out.settings.max - out.settings.min;
            const double span = std::max(ext.x(), std::max(ext.y(), ext.z()));
            const double cells = out.settings.res * span;
            // (roi_resolution(...) picks the resolution itself, within a
            // vertex budget: a fine one is what it means)
            if (span > 0 && cells > 5000 && !script.contains("roi_resolution(")) {
                const double good = 1000.0 / span;
                out.warnings.append({
                    QString("<b>Warning:</b> <code>resolution %1</code> means %2 voxels across the "
                            "render region (%3 wide): meshing will take very long.<br>"
                            "&nbsp;&nbsp;&nbsp;&nbsp;About 1000 across is plenty, e.g. "
                            "<code>view.set_resolution(%4)</code>, or "
                            "<code>roi_resolution(...)</code> for imports.")
                        .arg(out.settings.res).arg(qlonglong(cells)).arg(span, 0, 'g', 4)
                        .arg(good, 0, 'g', 2),
                    QString()});
            }
        }

        if (!out.warnings.isEmpty() && !script.contains("from fieldes import") &&
            !script.contains("import fieldes")) {
            out.warnings.insert(0, {
                "<b>Warning:</b> Missing <code>from fieldes import *</code>",
                "from fieldes import *\n"});
        }

        // A stopped script hasn't reached its settings yet: no warnings
        // about them until it has
        if (out.pausedLine > 0) {
            out.warnings.clear();
        }
      }
    } else {
        const auto tb = capturePythonTraceback();
        out.error = {tb, errorRangeFromTraceback(tb)};
    }

    Py_DECREF(vars_list);
    Py_XDECREF(ret);
    PyGILState_Release(gstate);

    emit(done(out));
}

QString Interpreter::callSupport(const QString& function, const QString& arg, QString* error)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    QString out;
    PyObject* mod = PyImport_ImportModule("fieldes.app_support");
    PyObject* fn = mod ? PyObject_GetAttrString(mod, function.toUtf8().constData()) : nullptr;
    PyObject* res = fn ? PyObject_CallFunction(fn, "s", arg.toUtf8().constData()) : nullptr;
    if (res && PyUnicode_Check(res))
    {
        out = QString::fromUtf8(PyUnicode_AsUTF8(res));
    }
    else
    {
        QString why = "the call failed";
        if (PyErr_Occurred())
        {
            PyObject *type = nullptr, *value = nullptr, *tb = nullptr;
            PyErr_Fetch(&type, &value, &tb);
            PyObject* text = value ? PyObject_Str(value) : nullptr;
            if (text && PyUnicode_Check(text)) why = QString::fromUtf8(PyUnicode_AsUTF8(text));
            Py_XDECREF(text);
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(tb);
        }
        PyErr_Clear();
        if (error) *error = why;
    }
    Py_XDECREF(res);
    Py_XDECREF(fn);
    Py_XDECREF(mod);
    PyGILState_Release(gstate);
    return out;
}

void Interpreter::recordShape(
        PyObject* obj,
        Result &out,
        std::map<libfive::Tree::Id, float>& vars,
        int line)
{
    if (PyObject_IsInstance(obj, m_shapeClass)) {
        const auto ptr_obj = PyObject_GetAttrString(obj, "ptr");
        const auto ptr = PyLong_AsVoidPtr(ptr_obj);
        const auto tree = static_cast<libfive_tree>(ptr);
        const auto shape = new Shape(libfive::Tree(tree), vars);
        shape->setSourceLine(line);

        // A thin part meshed on its own (fieldes.stdlib.cad_import):
        // (box lo, box hi, resolution, cube side, thinnest feature)
        // (a part placed with handles() has the hint of the part it was placed from)
        // (a part edited with expose() and placed with handles() is two steps from the part)
        PyObject* hintOwner = obj;
        Py_INCREF(hintOwner);
        for (int depth = 0; hintOwner && depth < 8 && !PyObject_HasAttrString(hintOwner, "_render_hint") &&
                            PyObject_HasAttrString(hintOwner, "_placed_from"); ++depth)
        {
            PyObject* from = PyObject_GetAttrString(hintOwner, "_placed_from");
            Py_DECREF(hintOwner);
            hintOwner = from;
        }
        if (hintOwner && PyObject_HasAttrString(hintOwner, "_render_hint"))
        {
            PyObject* h = PyObject_GetAttrString(hintOwner, "_render_hint");
            if (h && PyTuple_Check(h) && PyTuple_Size(h) == 5)
            {
                auto vec = [](PyObject* t) {
                    QVector3D v;
                    if (t && PyTuple_Check(t) && PyTuple_Size(t) == 3)
                        v = QVector3D(float(PyFloat_AsDouble(PyTuple_GetItem(t, 0))),
                                      float(PyFloat_AsDouble(PyTuple_GetItem(t, 1))),
                                      float(PyFloat_AsDouble(PyTuple_GetItem(t, 2))));
                    return v;
                };
                shape->setRenderHint(vec(PyTuple_GetItem(h, 0)), vec(PyTuple_GetItem(h, 1)),
                                     float(PyFloat_AsDouble(PyTuple_GetItem(h, 2))),
                                     float(PyFloat_AsDouble(PyTuple_GetItem(h, 3))),
                                     float(PyFloat_AsDouble(PyTuple_GetItem(h, 4))));
            }
            Py_XDECREF(h);
            PyErr_Clear();
        }
        Py_XDECREF(hintOwner);
        PyErr_Clear();

        // Handles of a placed part (fieldes.stdlib.handles): (mode, about (3),
        // move (3), rotate (3), scale (3)), each number a var Shape or a plain float
        // (the mode is 'gizmo', 'handles' or 'lock'; an older script's first item
        // was show, a boolean, and it had no scale)
        if (PyObject_HasAttrString(obj, "_handles"))
        {
            PyObject* h = PyObject_GetAttrString(obj, "_handles");
            if (h && PyTuple_Check(h) && (PyTuple_Size(h) == 4 || PyTuple_Size(h) == 5))
            {
                Shape::Handles hd;
                hd.present = true;
                PyObject* m = PyTuple_GetItem(h, 0);
                if (PyUnicode_Check(m))
                {
                    const QString name = QString::fromUtf8(PyUnicode_AsUTF8(m));
                    hd.mode = name == "lock" ? Shape::Handles::LOCK
                            : name == "handles" ? Shape::Handles::NATIVE : Shape::Handles::GIZMO;
                }
                else
                {
                    hd.mode = PyObject_IsTrue(m) == 1 ? Shape::Handles::GIZMO : Shape::Handles::LOCK;
                }
                PyObject* about = PyTuple_GetItem(h, 1);
                if (PyTuple_Check(about) && PyTuple_Size(about) == 3)
                {
                    hd.about = QVector3D(float(PyFloat_AsDouble(PyTuple_GetItem(about, 0))),
                                         float(PyFloat_AsDouble(PyTuple_GetItem(about, 1))),
                                         float(PyFloat_AsDouble(PyTuple_GetItem(about, 2))));
                }
                auto idOf = [](PyObject* v) -> libfive::Tree::Id {
                    if (!v || !PyObject_HasAttrString(v, "ptr")) return nullptr;
                    PyObject* p = PyObject_GetAttrString(v, "ptr");
                    libfive::Tree::Id id = nullptr;
                    if (p && PyLong_Check(p)) id = static_cast<libfive::Tree::Id>(PyLong_AsVoidPtr(p));
                    Py_XDECREF(p);
                    return id;
                };
                PyObject* mv = PyTuple_GetItem(h, 2);
                PyObject* rt = PyTuple_GetItem(h, 3);
                PyObject* sc = PyTuple_Size(h) == 5 ? PyTuple_GetItem(h, 4) : nullptr;
                for (int a = 0; a < 3; ++a)
                {
                    if (PyTuple_Check(mv) && PyTuple_Size(mv) == 3) hd.move[a] = idOf(PyTuple_GetItem(mv, a));
                    if (PyTuple_Check(rt) && PyTuple_Size(rt) == 3) hd.rotate[a] = idOf(PyTuple_GetItem(rt, a));
                    if (sc && PyTuple_Check(sc) && PyTuple_Size(sc) == 3) hd.scale[a] = idOf(PyTuple_GetItem(sc, a));
                }
                shape->setHandles(hd);
            }
            Py_XDECREF(h);
            PyErr_Clear();
        }

        // Exact regions (fieldes.stdlib.cad_import.exclude): each one's
        // _flat() is (path, solid, instance, matrix (16), region (16),
        // lo (3), hi (3), quality), every number a float or a Shape
        if (PyObject_HasAttrString(obj, "_exact"))
        {
            auto toTree = [](PyObject* v) {
                if (v && PyNumber_Check(v) && !PyObject_HasAttrString(v, "ptr"))
                    return libfive::Tree(float(PyFloat_AsDouble(v)));
                PyObject* p = v ? PyObject_GetAttrString(v, "ptr") : nullptr;
                libfive::Tree t = libfive::Tree(0.0f);
                if (p && PyLong_Check(p)) t = libfive::Tree(static_cast<libfive_tree>(PyLong_AsVoidPtr(p)));
                Py_XDECREF(p);
                return t;
            };
            auto trees = [&](PyObject* tuple, size_t n) {
                std::vector<libfive::Tree> out;
                for (size_t k = 0; k < n; k++)
                    out.push_back(toTree(tuple && PyTuple_Check(tuple) && size_t(PyTuple_Size(tuple)) > k
                                             ? PyTuple_GetItem(tuple, Py_ssize_t(k)) : nullptr));
                return out;
            };
            std::vector<Shape::ExactRegion> regions;
            PyObject* list = PyObject_GetAttrString(obj, "_exact");
            if (list && PyList_Check(list))
            {
                for (Py_ssize_t k = 0; k < PyList_Size(list); k++)
                {
                    PyObject* flat = PyObject_CallMethod(PyList_GetItem(list, k), "_flat", NULL);
                    if (flat && PyTuple_Check(flat) && PyTuple_Size(flat) == 7)
                    {
                        Shape::ExactRegion r;
                        PyObject* path = PyTuple_GetItem(flat, 0);
                        r.path = path && PyUnicode_Check(path) ? PyUnicode_AsUTF8(path) : "";
                        r.solid = int(PyLong_AsLong(PyTuple_GetItem(flat, 1)));
                        r.instance = int(PyLong_AsLong(PyTuple_GetItem(flat, 2)));
                        r.matrix = trees(PyTuple_GetItem(flat, 3), 16);
                        r.region = trees(PyTuple_GetItem(flat, 4), 16);
                        PyObject* fieldObj = PyTuple_GetItem(flat, 5);
                        r.field = fieldObj && fieldObj != Py_None ? toTree(fieldObj) : libfive::Tree::invalid();
                        r.quality = toTree(PyTuple_GetItem(flat, 6));
                        regions.push_back(r);
                    }
                    else
                    {
                        PyErr_Print();
                    }
                    Py_XDECREF(flat);
                }
            }
            Py_XDECREF(list);
            shape->setExactRegions(regions);
        }

        // Shown coloured by a field (fieldes.stdlib.fea.colored)
        if (PyObject_HasAttrString(obj, "_color_field"))
        {
            PyObject* field = PyObject_GetAttrString(obj, "_color_field");
            PyObject* fptr = field ? PyObject_GetAttrString(field, "ptr") : nullptr;
            if (fptr && PyLong_Check(fptr))
            {
                const auto ft = static_cast<libfive_tree>(PyLong_AsVoidPtr(fptr));
                float lo = 0, hi = 1;
                bool autoRange = true;
                PyObject* range = PyObject_GetAttrString(obj, "_color_range");
                if (range && PyTuple_Check(range) && PyTuple_Size(range) == 2)
                {
                    lo = float(PyFloat_AsDouble(PyTuple_GetItem(range, 0)));
                    hi = float(PyFloat_AsDouble(PyTuple_GetItem(range, 1)));
                    autoRange = false;
                }
                Py_XDECREF(range);
                auto str = [&](const char* name) {
                    QString out;
                    PyObject* v = PyObject_GetAttrString(obj, name);
                    if (v && PyUnicode_Check(v)) out = QString::fromUtf8(PyUnicode_AsUTF8(v));
                    Py_XDECREF(v);
                    return out;
                };
                shape->setColorField(libfive::Tree(ft), lo, hi, autoRange,
                                     str("_color_label"), str("_color_map"));

                // A structural result (Result.show): every field, the
                // displacements and the elements
                PyObject* fields = PyObject_GetAttrString(obj, "_color_fields");
                if (fields && PyList_Check(fields) && PyList_Size(fields) > 0)
                {
                    auto treeOf = [](PyObject* shapeObj) {
                        PyObject* p = shapeObj ? PyObject_GetAttrString(shapeObj, "ptr") : nullptr;
                        libfive::Tree t = libfive::Tree::invalid();
                        if (p && PyLong_Check(p)) t = libfive::Tree(static_cast<libfive_tree>(PyLong_AsVoidPtr(p)));
                        Py_XDECREF(p);
                        return t;
                    };
                    std::vector<Shape::FieldChannel> channels;
                    std::vector<Py_ssize_t> channelSource;    // which entry of `fields` each channel is
                    int current = 0;
                    const QString currentName = str("_color_field_name");
                    for (Py_ssize_t k = 0; k < PyList_Size(fields); k++)
                    {
                        PyObject* item = PyList_GetItem(fields, k);   // (name, label, shape, lo, hi)
                        if (!PyTuple_Check(item) || PyTuple_Size(item) < 5) continue;
                        Shape::FieldChannel c;
                        c.name = QString::fromUtf8(PyUnicode_AsUTF8(PyTuple_GetItem(item, 0)));
                        c.label = QString::fromUtf8(PyUnicode_AsUTF8(PyTuple_GetItem(item, 1)));
                        c.tree = treeOf(PyTuple_GetItem(item, 2));
                        c.lo = float(PyFloat_AsDouble(PyTuple_GetItem(item, 3)));
                        c.hi = float(PyFloat_AsDouble(PyTuple_GetItem(item, 4)));
                        if (!c.tree.is_valid()) continue;
                        if (c.name == currentName) current = int(channels.size());
                        channels.push_back(c);
                        channelSource.push_back(k);
                    }
                    libfive::Tree u[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(),
                                          libfive::Tree::invalid()};
                    PyObject* deform = PyObject_GetAttrString(obj, "_deform");
                    if (deform && PyTuple_Check(deform) && PyTuple_Size(deform) == 3)
                    {
                        for (int a = 0; a < 3; a++) u[a] = treeOf(PyTuple_GetItem(deform, a));
                    }
                    Py_XDECREF(deform);
                    auto num = [&](const char* name, double fallback) {
                        PyObject* v = PyObject_GetAttrString(obj, name);
                        double out = (v && PyNumber_Check(v)) ? PyFloat_AsDouble(v) : fallback;
                        Py_XDECREF(v);
                        return out;
                    };
                    Shape::ElementGrid grid;
                    PyObject* g = PyObject_GetAttrString(obj, "_fea_grid");
                    // (corner, cell size, cell counts, fill fractions, element, [each field's value in every element])
                    if (g && PyTuple_Check(g) && PyTuple_Size(g) == 6)
                    {
                        PyObject* lo3 = PyTuple_GetItem(g, 0);
                        PyObject* dims = PyTuple_GetItem(g, 2);
                        PyObject* frac = PyTuple_GetItem(g, 3);
                        PyObject* kind = PyTuple_GetItem(g, 4);
                        PyObject* vals = PyTuple_GetItem(g, 5);
                        if (PyTuple_Check(lo3) && PyTuple_Size(lo3) == 3 && PyTuple_Check(dims) &&
                            PyTuple_Size(dims) == 3 && PyBytes_Check(frac) && PyUnicode_Check(kind) &&
                            PyList_Check(vals))
                        {
                            grid.lo = QVector3D(float(PyFloat_AsDouble(PyTuple_GetItem(lo3, 0))),
                                                float(PyFloat_AsDouble(PyTuple_GetItem(lo3, 1))),
                                                float(PyFloat_AsDouble(PyTuple_GetItem(lo3, 2))));
                            grid.h = float(PyFloat_AsDouble(PyTuple_GetItem(g, 1)));
                            for (int a = 0; a < 3; a++) grid.dims[a] = int(PyLong_AsLong(PyTuple_GetItem(dims, a)));
                            const size_t n = size_t(grid.dims[0]) * grid.dims[1] * grid.dims[2];
                            if (size_t(PyBytes_Size(frac)) == n * sizeof(float))
                            {
                                grid.fraction.resize(n);
                                std::memcpy(grid.fraction.data(), PyBytes_AsString(frac), n * sizeof(float));
                                grid.tetrahedra = QString::fromUtf8(PyUnicode_AsUTF8(kind)) == "tet";
                                grid.text = str("_fea_element_text");
                                size_t active = 0;
                                for (float f : grid.fraction) active += f > 0;
                                const size_t expect = active * (grid.tetrahedra ? 6 : 1);
                                for (size_t c = 0; c < channels.size(); c++)
                                {
                                    std::vector<float> v;
                                    const Py_ssize_t k = channelSource[c];
                                    PyObject* b = k < PyList_Size(vals) ? PyList_GetItem(vals, k) : nullptr;
                                    if (b && PyBytes_Check(b) && size_t(PyBytes_Size(b)) == expect * sizeof(float))
                                    {
                                        v.resize(expect);
                                        std::memcpy(v.data(), PyBytes_AsString(b), expect * sizeof(float));
                                    }
                                    grid.values.push_back(std::move(v));
                                }
                            }
                        }
                    }
                    Py_XDECREF(g);
                    // (vertices, tetrahedra, boundary triangles, the tetrahedron of each, text,
                    // [each field's value in every tetrahedron], the displacement at every vertex)
                    PyObject* mesh = PyObject_GetAttrString(obj, "_fea_mesh");
                    if (mesh && PyTuple_Check(mesh) && PyTuple_Size(mesh) == 7)
                    {
                        auto bytesAt = [&](Py_ssize_t i) -> PyObject* {
                            PyObject* b = PyTuple_GetItem(mesh, i);
                            return b && PyBytes_Check(b) ? b : nullptr;
                        };
                        PyObject *bv = bytesAt(0), *bt = bytesAt(1), *bf = bytesAt(2), *bft = bytesAt(3),
                                 *bd = bytesAt(6);
                        PyObject* text = PyTuple_GetItem(mesh, 4);
                        PyObject* vals = PyTuple_GetItem(mesh, 5);
                        if (bv && bt && bf && bft && bd && text && PyUnicode_Check(text) && vals && PyList_Check(vals))
                        {
                            auto floats = [](PyObject* b, std::vector<float>& out) {
                                out.resize(size_t(PyBytes_Size(b)) / sizeof(float));
                                std::memcpy(out.data(), PyBytes_AsString(b), out.size() * sizeof(float));
                            };
                            auto ints = [](PyObject* b, std::vector<int32_t>& out) {
                                out.resize(size_t(PyBytes_Size(b)) / sizeof(int32_t));
                                std::memcpy(out.data(), PyBytes_AsString(b), out.size() * sizeof(int32_t));
                            };
                            grid = Shape::ElementGrid();
                            grid.isMesh = true;
                            floats(bv, grid.mverts);
                            ints(bt, grid.mtets);
                            ints(bf, grid.mfaces);
                            ints(bft, grid.mfaceTet);
                            floats(bd, grid.mdisp);
                            grid.text = QString::fromUtf8(PyUnicode_AsUTF8(text));
                            for (size_t c = 0; c < channels.size(); c++)
                            {
                                std::vector<float> v;
                                const Py_ssize_t k = channelSource[c];
                                PyObject* b = k < PyList_Size(vals) ? PyList_GetItem(vals, k) : nullptr;
                                if (b && PyBytes_Check(b)) floats(b, v);
                                grid.values.push_back(std::move(v));
                            }
                        }
                    }
                    Py_XDECREF(mesh);
                    PyErr_Clear();
                    if (!channels.empty())
                    {
                        shape->setResult(channels, current, u[0], u[1], u[2],
                                         float(num("_deform_scale", 0)), float(num("_deform_auto", 1)),
                                         grid);
                    }
                }
                Py_XDECREF(fields);
            }
            Py_XDECREF(fptr);
            Py_XDECREF(field);
            PyErr_Clear();
        }
        shape->moveToThread(QApplication::instance()->thread());
        out.shapes.push_back(shape);

        Py_DECREF(ptr_obj);
        PyErr_Print();
    }
}

}   // namespace Python
}   // namespace FielDes
