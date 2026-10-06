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

#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QTimer>

#include "fieldes/args.hpp"
#include "fieldes/language.hpp"

namespace libfive { class Mesh; }

namespace FielDes {
class Editor;
class Tutorial;
class View;

class Window : public QMainWindow
{
    Q_OBJECT
public:
    /*
     *  If target is the null QString, then loads the tutorial file
     *  on first run.
     */
    explicit Window(Arguments args);

    /*
     *  Loads a file by path, properly checking if the existing document
     *  is unsaved and asking the user to save it in that case.
     */
    bool openFile(const QString& name);

protected slots:
    bool onOpen(bool=false);
    bool onOpenViewer(bool=false);
    /*  File > Open example file: the file dialog in the examples folder that is next to the program  */
    bool onOpenExample(bool=false);
    bool onRevert(bool=false);
    bool onSave(bool=false);
    bool onSaveAs(bool=false);
    bool onNew(bool=false);
    void onExport(bool=false);
    void onAbout(bool=false);
    bool loadTourModel();
    bool onLoadDefault(bool=false);
    void onAutoLoad(const QString&);
    void onAutoLoadPath(const QString&);
    void onLanguageChanged();
    void onQuit(bool=false);

    /*  Cancels any in-progress render (bound to Escape), so a shape that's
     *  too expensive to mesh at the current settings can be stopped without
     *  force-closing Studio. This clears the current shapes/mesh; editing
     *  the script afterwards triggers a fresh render. */
    void onCancelRender(bool=false);

    /*  Autosaves the current file (if one is set and there are unsaved
     *  changes) every few seconds, so a frozen/killed Studio doesn't lose
     *  your work -- Studio previously had no autosave at all. */
    void onAutosave();

    void onExportReady(QList<const libfive::Mesh*> shapes);

    /*  File > Import model: asks for a STEP / STL / OBJ file and adds the
     *  code importing it (with an automatic region of interest)  */
    void onImportModel(bool=false);

    /*  Watches evaluations to finish a pending import: once the parts of a
     *  freshly imported STEP file are known, adds one display line per part
     *  that could be rebuilt  */
    void onSceneChanged(QString json);

signals:
    void exportDone();
    void setAutoload(bool);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool dropEvent_(QDropEvent *event);
    void closeEvent(QCloseEvent* event) override;

    /*  File > Open recent  */
    void addRecentFile(const QString& f);
    /*  The files imported with Import model (and the arrows beside the Open and Import icons list them, and the scripts opened):
     *  the newest first, only those that still exist  */
    void addRecentImport(const QString& f);
    void showRecent(QWidget* below, bool imports);
    void updateRecentMenu();
    class QMenu* recent_menu=nullptr;

    /*  True when driven by FIELDES_AUTOMATION (a developer test run), which
     *  must not touch the user's layout or recent files  */
    bool automated=false;

    QMessageBox::StandardButton checkUnsaved();
    void setFilename(const QString& str);
    QString workingDirectory() const;

    bool loadFile(QString f, bool reload=false);

    /*  Adds import code for a model file to the script  */
    void importModel(const QString& path);
    bool saveFile(QString f);

    /* Changes the Editor language and reloads the default script
     * (warning if there are unsaved changes) */
    bool setLanguage(Language::Type t);

    /*  Resets the script to the default for the given language,
     *  clearing the filename and autoload parameters.  If the language
     *  is LANGUAGE_NONE, then leave it unchanged. */
    bool reset(Language::Type t);

    /*  Filename of the current file, or empty string */
    QString filename;

    /*  File watcher to check for changes to filename */
    QFileSystemWatcher watcher;

    /*  The STEP (and mesh) files the script imports: one saved by another program runs the script again,
     *  which reads it again (an import is cached under the file's hash, so a changed file is a new import)  */
    QFileSystemWatcher m_stepWatcher;
    QTimer m_stepReload;
    QStringList m_stepChanged;
    void watchImports(const QString& sceneJson);

    /*  The custom blocks (python/fieldes/blocks.py): the files of the blocks folder are watched, and a script that uses a
     *  block runs again when its file is saved.  Settings > Blocks folder chooses the folder  */
    QFileSystemWatcher m_blocksWatcher;
    QTimer m_blocksReload;
    QStringList m_blocksChanged;
    QString m_blocksFolder;
    void watchBlocks();
    void chooseBlocksFolder();
    void setBlocksFolder(const QString& path);

    /*  True when we should automatically reload the file on changes */
    bool autoreload=false;

    /*  Used to store the export target while meshes are being generated
     *  and the main event loop is blocked by a progress dialog */
    QString export_filename;

    Editor* editor;
    View* view;
    Tutorial* tour=nullptr;
    bool closing=false;

    /* Used to (re)store the state of the application */
    QSettings settings;

    /*  Fires onAutosave() periodically; see its declaration above */
    QTimer autosaveTimer;

    /*  Model-tree description of the last evaluation, and the parts-list
     *  variable of an import whose part display lines are still to come  */
    QString m_lastScene;
    QString m_pendingImport;
};
}   // namespace FielDes
