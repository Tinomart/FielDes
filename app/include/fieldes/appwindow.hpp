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

#include <functional>

#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMainWindow>
#include <QSet>
#include <QTextCursor>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>

#include "fieldes/documentation.hpp"
#include "fieldes/result.hpp"

class QCheckBox;
class QLabel;
class QSplitter;
class QTextDocument;
class QPlainTextEdit;
class QProgressBar;
class QScrollArea;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace FielDes {
namespace Python { class Interpreter; }
class Shape;
class View;
class FieldPanel;

/*
 *  The window of a FielDes app: a script that people who never open the editor can run.  The script declares its inputs (fieldes.app); this
 *  window shows them as a form, runs the script with the values chosen when Run is pressed, and lists the log and the files that were made.
 *  It is a program of its own (a second FielDes process) so it can sit beside the editor.
 *
 *  It opens a `.fieldesapp` file (a script bundled with its blocks and files: fieldes.app_package) or a plain script, to try an app
 *  as the person who will run it sees it.  Watch: the app runs again by itself when a file chosen for an input changes, and the form is
 *  read again when the app file itself is replaced (an engineer exporting it again).
 */
class AppWindow : public QMainWindow
{
    Q_OBJECT
public:
    /*  `presets`: the command line after the file (`--Radius 7 --Model part.step`): values for the form; `run`: press Run once it is open  */
    AppWindow(const QString& path, const QStringList& presets, bool run);
    ~AppWindow() override;

    /*  Whether the app could be opened (the window says why when it could not)  */
    bool isOpen() const { return m_open; }

    /*  The folder the form's relative output paths mean (Documents)  */
    QString documentsFolder() const { return m_documents; }

signals:
    void evalRequested(QString text);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onReady(QStringList keywords, Documentation docs);
    void onDone(Result result);
    void onRun();
    void onStop();
    void onPoll();
    void onWatchedChanged(const QString& path);
    void onWatchTriggered();
    void onOpen();
    void onLiveTimer();
    void onResetPicture();

private:
    struct Field
    {
        QString key, label, kind;
        QJsonObject spec;
        QWidget* editor = nullptr;
        QLabel* error = nullptr;
        std::function<QJsonValue()> get;
        std::function<void(const QJsonValue&)> set;
    };

    /*  The viewport (app.viewport): made when the app asks for one, with the models the script gives it.  A run that is only for the picture
     *  (the app was opened, an input changed, a model was dragged) has app.running() False; Run makes the work, and the picture too  */
    void ensureViewport();
    void scheduleLive(int ms);
    void startPicture();
    void setPicture(bool on);
    void finishPicture(Result& r, bool terminated);
    void showInViewport(Result& r, bool terminated);
    void applySelection();
    void onInputEdited();
    /*  What a drag in the viewport changes is written into the text of the script the app runs, as the editor does: the places of the numbers
     *  (the run says where they are in the text it ran) are cursors of a document, which every edit moves along  */
    void setVarSpans(const QMap<libfive::Tree::Id, QRect>& rects);
    void applyVarValues(const QMap<libfive::Tree::Id, float>& vs);

    bool openApp(const QString& path, bool again);
    void buildForm(bool again);
    void applyPresets();
    void applySaved();
    void saveValues();
    QJsonObject values() const;
    void setError(const QString& key, const QString& why);
    void clearErrors();
    void setRunning(bool running);
    void setStatus(const QString& text, const QString& color = QString());
    void log(const QString& text, const QString& color = QString());
    void showOutputs();
    void updateWatch(const QStringList& files);
    void showProblem(const QString& title, const QString& text);
    void registerAutomation();
    QString callSupport(const QString& function, const QJsonObject& arg, QString* error = nullptr);
    Field* field(const QString& key);

    // The interpreter: its own thread, as in the editor
    Python::Interpreter* m_interp = nullptr;
    QThread m_thread;
    bool m_ready = false;           // the interpreter has started: Run works
    bool m_open = false;
    bool m_running = false;
    bool m_runAgain = false;        // a change came while the app ran: it runs again when it is done
    bool m_reloadAfter = false;
    bool m_autoRun = false;
    bool m_stopped = false;
    bool m_closing = false;         // the window is to close once the run has been stopped

    // The viewport
    bool m_hasViewport = false;
    bool m_picture = false;         // the run that is going only makes the picture
    bool m_liveAgain = false;       // another picture is wanted when this one is done
    bool m_runAfter = false;        // Run was pressed while a picture was being made
    bool m_dragging = false;
    bool m_building = false;        // the form is being made: what it sets is not an edit
    bool m_firstPicture = true;
    bool m_pictureError = false;
    QString m_lastPictureError;
    QTimer m_liveTimer;
    QSet<int> m_selected;
    QSplitter* m_split = nullptr;
    QWidget* m_viewHost = nullptr;
    View* m_view = nullptr;
    QLabel* m_viewHint = nullptr;
    QPushButton* m_resetButton = nullptr;
    FieldPanel* m_fieldPanel = nullptr;
    QStringList m_openFields;           // (the fields whose icons were clicked: their viewers are open)
    QString m_originalScript;
    QTextDocument* m_doc = nullptr;
    struct VarSpan
    {
        QTextCursor start, end;
        QString text;
    };
    QMap<libfive::Tree::Id, VarSpan> m_varSpans;
    QElapsedTimer m_clock;
    QTimer m_poll;
    QTimer m_watchDebounce;
    QFileSystemWatcher m_watcher;
    QString m_changed;              // what changed (for the log)

    // The app
    QString m_path;                 // the file opened
    QString m_kind;                 // "app" or "script"
    QString m_script;               // its text
    QString m_id;
    QString m_title;
    QString m_documents;
    QJsonObject m_manifest;
    QStringList m_presets;
    QList<Field> m_fields;

    // The window
    QWidget* m_header = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_formHost = nullptr;
    QVBoxLayout* m_formLayout = nullptr;
    QWidget* m_form = nullptr;
    QPushButton* m_runButton = nullptr;
    QCheckBox* m_watchBox = nullptr;
    QProgressBar* m_bar = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_outputsBox = nullptr;
    QVBoxLayout* m_outputsLayout = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QWidget* m_problems = nullptr;
};

}   // namespace FielDes
