/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2017  Matt Keeter

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
#include <QActionGroup>
#include <QDockWidget>
#include <QAbstractButton>
#include <QRadioButton>
#include <QSlider>
#include <QComboBox>
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QRegularExpression>
#include <QContextMenuEvent>
#include <QTextBlock>
#include <QTreeWidget>
#include <QLineEdit>
#include <iostream>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScreen>
#include <QSplitter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QDir>
#include <QClipboard>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>
#include <QTimer>
#include <QFrame>
#include <QToolButton>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <memory>

#include "libfive/step/step_progress.hpp"

#include "fieldes/window.hpp"
#include "fieldes/automation.hpp"
#include "fieldes/documentation.hpp"
#include "fieldes/editor.hpp"
#include "fieldes/findbar.hpp"
#include "fieldes/icons.hpp"
#include "fieldes/theme.hpp"
#include "fieldes/tutorial.hpp"
#include "fieldes/scenetree.hpp"
#include "fieldes/script.hpp"
#include "fieldes/section.hpp"
#include "fieldes/shape.hpp"
#include "fieldes/shortcuts.hpp"
#include "fieldes/view.hpp"

#include "libfive.h"

#define CHECK_UNSAVED() \
switch (checkUnsaved())                                                     \
{                                                                           \
    case QMessageBox::Save:     if (!onSave()) return false;                \
    case QMessageBox::Ok:       /* FALLTHROUGH */                           \
    case QMessageBox::Discard:  break;                                      \
    case QMessageBox::Cancel:   return false;                               \
    default:    assert(false);                                              \
}

namespace FielDes {

static QString guideHtml()
{
    auto keys = [](const QString& id) {
        for (const auto& e : Shortcuts::entries())
        {
            if (e.id == id && e.action)
            {
                const QString t = Shortcuts::toText(e.action->shortcuts().mid(0, 1));
                return t.isEmpty() ? QString("<i>unbound</i>") : "<b>" + t.toHtmlEscaped() + "</b>";
            }
        }
        return QString("<i>unbound</i>");
    };

    QString h;
    h += "<style>h2{margin-top:16px; margin-bottom:2px} li{margin-bottom:3px} td{padding:2px 14px 2px 0} "
         "code{background:#eee}</style>";
    h += "<h1>FielDes guide</h1>"
         "<p>The script is the model. Whatever you do in the viewport is written into the script, so "
         + keys("edit.undo") + " undoes it. The <b>docs</b> folder has the full documentation. <b>Help &gt; Guided tour</b> walks through the basics on the program itself.</p>";

    h += "<h2>Making things</h2><ul>"
         "<li><b>Right-click</b> empty space in the viewport: <b>New 3D shape</b>, <b>New 2D shape</b>, <b>New point</b>, "
         "<b>New surface</b>, <b>New field</b> and <b>New custom block</b> write the call into the script where the cursor is "
         "(and select it, ready to drag); <b>Add operation</b> and <b>Add simulation</b> work on the selected model. "
         "Right-click a model for <b>Operation</b> (offsets, moving, field math ...) and <b>Simulation</b> (static, modal, "
         "topology optimization, thermal, flow) on it, written with that model as the argument.</li>"
         "<li><b>The same menu opens in the text editor and in the model tree</b>: right-click a line of the script that defines "
         "a model, or its row in the tree, and it is as if you had right-clicked the model in the viewport. On a line with no "
         "model it is the menu of empty space, and what you make goes under that line.</li>"
         "<li><b>A field is not drawn</b>: select it in the model tree and the section viewer opens on it, showing its value at "
         "every point of a plane you can move through it. Fields can be multiplied, added ... like numbers "
         "(<code>a * b</code>, <code>2 ** a</code>); <code>field_from_body(part)</code> makes a field of a body's values.</li>"
         "<li>Every kind of thing has its own colour and icon in the tree and the menus: "
         "<span style='color:#4aa8e8'><b>3D shape</b></span>, <span style='color:#35c4b3'><b>2D shape</b></span>, "
         "<span style='color:#82cc58'><b>field</b></span>, <span style='color:#b583ee'><b>surface</b></span>, "
         "<span style='color:#f4b73a'><b>point</b></span>, <span style='color:#ee6a5e'><b>simulation</b></span>, "
         "<span style='color:#c9a66b'><b>material</b></span>, <span style='color:#e08f58'><b>conditions</b></span>, "
         "<span style='color:#d4b43c'><b>lattice cell</b></span>. "
         "A model made by one of your own blocks has a small <b>f</b> badge.</li>"
         "<li><b>Every number can be a field</b>: <code>offset(part, 0.5)</code> and "
         "<code>offset(part, ramp(z_field(), (0, 40), (0.2, 2)))</code> are written alike. A point can stand "
         "where a coordinate goes: <code>distance_to_point(p)</code>.</li></ul>";

    h += "<h2>Model tree</h2><ul>"
         "<li><b>Click</b> a row to select it and see its code. <b>Eye</b>: show or hide.</li>"
         "<li><b>Nesting is the structure of the calls</b>: the models an operation is made of are its children, in the "
         "order of its arguments. A model that several statements use has its row under the first, and a <b>shadow</b> "
         "(half transparent) under each other one: that statement's own reference. Every nest, renest and denest is an edit "
         "of the arguments: <b>drag a row onto an operation</b> to make it one of its inputs (added to a <code>union</code> and the "
         "like; never a menu); drag it out of a call to take it out; drop it "
         "<b>between the children</b> of an operation to put it at that place among the arguments. Dragging a shadow moves "
         "only that reference. <b>Ctrl+drag</b> moves nothing: it puts a new shadow where it is dropped (a model nothing else uses stays a top-level row, and the call gets a <code># shadow: name</code> comment). A shadow cannot go above "
         "its original (the first statement that uses the model): a message explains it (once, however many shadows it is about, also "
         "when a whole statement is dragged above the original) and offers the fix, which makes the first use the original and "
         "leaves a reference at the old one. A "
         "field dropped on an operation takes the place of one of its numbers (<code>offset(plate, 1.0)</code> becomes "
         "<code>offset(plate, swell)</code>). Between two top-level rows a model moves in the script (with what it is made "
         "of, when that is defined later). Taking a model out of an operation that cannot do without it (fixed inputs) "
         "<b>deletes that operation</b>, after a question (default: Delete; it can be hidden). The bin button and "
         + keys("view.delete") + " work on a shadow too: its reference is taken out. All of it is one undoable step.</li>"
         "<li><b>Several rows</b>: Ctrl+click adds one or takes it out, Shift+click selects from the last one clicked, "
         "as in a file list. In the viewport Ctrl+click does the same, and dragging draws a rectangle that selects "
         "every body lying wholly inside it (with Ctrl too: added to the selection). A click on empty space "
         "deselects all.</li>"
         "<li><b>Dragging a surface</b> of a model is always possible (selecting it gives it the numbers). The "
         "<b>gizmo button</b> (moves, rotates and scales) sets when the <b>gizmo</b> is shown: <b>click</b> (the "
         "default: while the model is selected), <b>never</b> or <b>always</b>; it has priority over a surface "
         "under it. <b>Lock button</b>: a locked model cannot be dragged at all; unlocked, it has the gizmo mode "
         "it had. The <b>dot</b> in the middle of a gizmo moves the model freely, in the plane facing the "
         "camera.</li>"
         "<li><b>Several models selected</b> are moved by one gizmo, at the middle of them, whatever gizmo mode "
         "each has when selected alone (a model with no numbers to move it by gets a gizmo line, in the mode it is "
         "in). The key <b>E</b> changes each one's own gizmo mode for when it is selected alone. A "
         "<b>locked</b> model cannot be in a selection of several: select it on its own to unlock it.</li>"
         "<li>Keys (viewport focused; they work on all selected models): " + keys("view.edit-toggle") +
         " goes round the gizmo modes (click, never, always), " + keys("view.edit-lock") + " lock / unlock, " +
         keys("view.visible") + " show / hide, " +
         keys("view.cache") + " render cache on / off, " + keys("view.isolate") + " shows only the selected, " +
         keys("view.delete") + " deletes the selected models from the script. "
         "A toggle first puts all the selected models in its on state (shown, locked, cached) if they are "
         "not all in it; only then does it turn them all off.</li>"
         "<li><b>Rename</b>: double-click a name (or F2), type, Enter: every use in the script changes. "
         "<b>Render settings</b>: open the row and edit the region, resolution and quality as numbers.</li>"
         "<li>A displayed expression gets a name first: <code>sphere(3)</code> becomes <code>sphere_1 = sphere(3)</code>.</li>"
         "<li><b>Reimport</b> reads a file again; the <b>bin</b> also clears its cache and handle edits.</li>"
         "<li>Grey: hidden, or outside the render region.</li></ul>";

    h += "<h2>Viewport</h2><ul>"
         "<li>Left-drag draws a selection rectangle, Shift+left-drag or middle-drag rotates, right-drag pans, the wheel zooms. Double-click frames a model or everything ("
         + keys("view.frame-all") + ").</li>"
         "<li>Standard views (click the viewport first): front " + keys("view.std-front") +
         ", right " + keys("view.std-right") + ", top " + keys("view.std-top") +
         ", isometric " + keys("view.std-iso") + ". The triad (top right) is clickable too.</li>"
         "<li>" + keys("view.cancel-render") + " cancels a slow render. Screenshots: " + keys("file.export-screenshot") +
         " (file), " + keys("file.copy-screenshot") + " (clipboard).</li></ul>";

    h += "<h2>Section and result cards</h2><ul>"
         "<li>" + keys("view.section") + " opens the section card: pick the axis, move the plane with the slider "
         "or its arrow, flip the kept side.</li>"
         "<li><b>Cut model</b> clips the geometry; <b>Field</b> paints the plane with the distance field, or with the "
         "analysis result inside the part.</li>"
         "<li><b>Whole elements</b> (with analysis elements shown) keeps elements whole at the plane.</li>"
         "<li>The result card switches the field, magnifies the deformation and shows the solver's elements.</li>"
         "<li>Hover a coloured model to read its value. Closing a legend (its x) stops the probing.</li></ul>";

    h += "<h2>Importing</h2><ul>"
         "<li>" + keys("file.import-model") + " or drop a file on the window.</li>"
         "<li><b>STEP</b>: every solid becomes a field, one part each; assemblies arrive assembled. Free-form faces "
         "are fitted and shaded by their deviation.</li>"
         "<li><code>exclude()</code> or <code>auto_exclude=True</code> locks the file's own exact surface (made a field) where the fit is poor.</li>"
         "<li><b>STL, OBJ, PLY, 3MF, GLB</b>: an exact distance field. Pass <code>file_units=</code> if the file is not in mm.</li></ul>";

    h += "<h2>Analysis</h2><ul>"
         "<li><code>static_analysis</code>, <code>modal_analysis</code>, <code>thermal_analysis</code>, "
         "<code>topology_optimization</code> and <code>fluid_analysis</code> work on any shape (mm, N, MPa).</li>"
         "<li>Supports and loads are shapes: <code>fixed(region)</code>, <code>force(region, (fx, fy, fz))</code>, "
         "<code>gravity()</code>.</li>"
         "<li>Results are fields: <code>result.show('von_mises')</code>, or <code>part - 0.02 * result.von_mises</code>.</li>"
         "<li>An unchanged analysis is not solved again.</li></ul>";

    h += "<h2>Custom blocks</h2><ul>"
         "<li>Put a <code>.py</code> file in the <b>blocks folder</b> (Settings &rarr; Blocks folder... chooses it; "
         "<i>Show the blocks folder</i> opens it) and every function in it is there in every script, with its call tip "
         "and completion: <code>def perforate(body, hole_radius=2.0): ...</code>. Save the file and the scripts that "
         "use it run again. A block whose first argument is a model is in <b>Add operation &rarr; Custom blocks</b>.</li></ul>";

    h += "<h2>Fields and lattices</h2><ul>"
         "<li>Any shape is a field. <code>depth_below(part)</code>, <code>ramp()</code>, <code>fit(data)</code> and "
         "analysis results can drive offsets, thicknesses and lattices.</li>"
         "<li><code>lattice(body, cell_periodic('gyroid'), cell_size=8, thickness=t, skin=1.5)</code> fills a body; "
         "<code>density=</code> sets the share of material instead. A cell is <code>cell_periodic(kind)</code>, "
         "<code>cell_non_periodic(kind)</code>, <code>cell_custom(region, geometry)</code> (any geometry in a box), "
         "<code>cell_custom_truss(nodes, beams)</code> or <code>cell_custom_tpms(equation)</code>.</li>"
         "<li><code>colored(part, field)</code> paints a part by a field.</li></ul>";

    h += "<h2>Editor</h2><ul>"
         "<li>Completion while typing (" + keys("edit.complete") + "): a function gets its brackets. Find (" + keys("edit.find") +
         "), multiple cursors (Alt+click, " + keys("edit.select-next-occurrence") + ").</li>"
         "<li>Go to definition (Ctrl+click, " + keys("edit.go-to-definition") + ") also opens other files, in tabs. The first tab "
         "(&#9654;) is the script that is rendered, the others are only for editing; " + keys("edit.back-to-script") +
         " goes back to it, " + keys("edit.close-tab") + " closes a tab.</li>"
         "<li>Breakpoint on a line: " + keys("edit.toggle-breakpoint") + "; continue: " + keys("edit.continue") + ".</li>"
         "<li><b>Sections</b>: a comment <code>#SECTION Title</code> gets a fold arrow in the gutter; it folds everything up to "
         "the next <code>#SECTION</code> (typing <code>#sec</code> completes it).</li>"
         "<li>Text size: " + keys("settings.font-bigger") + " and " + keys("settings.font-smaller") + ".</li></ul>";

    h += "<h2>Keyboard shortcuts</h2><p>Change them under Settings &rarr; Keyboard shortcuts ("
         + keys("settings.keyboard-shortcuts") + ").</p><table>";
    QString category;
    for (const auto& e : Shortcuts::entries())
    {
        if (!e.action) continue;
        if (e.category != category)
        {
            category = e.category;
            QString title = category;
            if (!title.isEmpty()) title[0] = title[0].toUpper();
            h += "<tr><td colspan=2><br><b>" + title.toHtmlEscaped() + "</b></td></tr>";
        }
        QString name = e.action->text();
        name.remove('&');
        h += "<tr><td>" + name.toHtmlEscaped() + "</td><td>" +
             Shortcuts::toText(e.action->shortcuts()).toHtmlEscaped() + "</td></tr>";
    }
    h += "</table>";
    return h;
}

Window::Window(Arguments args)
    : QMainWindow()
    , view(new View)
    , settings("FielDes", "FielDes")
{
    automated = !qEnvironmentVariable("FIELDES_AUTOMATION").isEmpty();

    editor = new Editor(Language::LANGUAGE_PYTHON);

    // the first languageChanged signal happens at construction, when we're not yet connected
    onLanguageChanged();
    connect(editor, &Editor::languageChanged,
            this, &Window::onLanguageChanged);

    resize(QGuiApplication::primaryScreen()->availableGeometry().size() * 0.8);

    setAcceptDrops(true);

    auto layout = new QSplitter(args.vertical ? Qt::Vertical : Qt::Horizontal);

    if (args.vertical)
    {
        layout->addWidget(view);
        layout->addWidget(editor);
    }
    else
    {
        layout->addWidget(editor);
        layout->addWidget(view);
    }

    layout->setChildrenCollapsible(false);
    layout->setHandleWidth(1);
    layout->setStretchFactor(args.vertical ? 1 : 0, 0);
    layout->setStretchFactor(args.vertical ? 0 : 1, 1);
    {   // (maximized: the editor takes under a third of the screen, the model the rest)
        const auto room = QGuiApplication::primaryScreen()->availableGeometry();
        if (args.vertical) layout->setSizes({int(room.height() * 0.70), int(room.height() * 0.30)});   // (view, editor)
        else layout->setSizes({int(room.width() * 0.30), int(room.width() * 0.70)});
    }
    editor->setMinimumWidth(320);
    setCentralWidget(layout);

    // Sync document modification state with window
    connect(editor, &Editor::modificationChanged,
            this, &QWidget::setWindowModified);

    // Sync settings from script to viewport
    connect(editor, &Editor::settingsChanged,
            view, &View::onSettingsFromScript);

    // Always run the autoload check, looking at the local
    // variable to decide whether or not to actually
    // reload the file.
    m_stepReload.setSingleShot(true);
    m_stepReload.setInterval(1500);           // (a program writing the file takes a moment; some replace it)
    connect(&m_stepWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (!m_stepChanged.contains(path)) m_stepChanged << path;
        if (QFileInfo::exists(path) && !m_stepWatcher.files().contains(path)) m_stepWatcher.addPath(path);
        m_stepReload.start();
    });
    connect(&m_stepReload, &QTimer::timeout, this, [this]{
        QStringList names;
        for (const auto& p : m_stepChanged) names << QFileInfo(p).fileName();
        m_stepChanged.clear();
        statusBar()->showMessage("Reloading: " + names.join(", ") + " changed on disk", 6000);
        editor->onTextChangedDebounce();
    });
    // The custom blocks: a script that uses a block runs again when the file that defines it is saved
    m_blocksReload.setSingleShot(true);
    m_blocksReload.setInterval(500);
    connect(&m_blocksWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (!m_blocksChanged.contains(path)) m_blocksChanged << path;
        if (QFileInfo::exists(path) && !m_blocksWatcher.files().contains(path)) m_blocksWatcher.addPath(path);
        m_blocksReload.start();
    });
    connect(&m_blocksWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        if (!m_blocksChanged.contains(path)) m_blocksChanged << path;
        m_blocksReload.start();
    });
    connect(&m_blocksReload, &QTimer::timeout, this, [this]{
        QStringList names;
        for (const auto& p : m_blocksChanged) names << QFileInfo(p).fileName();
        m_blocksChanged.clear();
        watchBlocks();      // (files that came or went)
        // (only a script that uses a block of the folder is run again)
        if (editor->callSupport("blocks_used", editor->getScript(), nullptr) != "1") return;
        statusBar()->showMessage("Running the script again: " + names.join(", ") + " changed", 6000);
        editor->onTextChangedDebounce();
    });
    connect(&watcher, &QFileSystemWatcher::fileChanged,
            this, &Window::onAutoLoad);
    connect(&watcher, &QFileSystemWatcher::directoryChanged,
            this, &Window::onAutoLoadPath);


    // Connect drag start + end signals, so the user can't edit
    // the script while dragging in the 3D viewport
    connect(view, &View::dragStart, editor, &Editor::onDragStart);
    connect(view, &View::dragEnd, editor, &Editor::onDragEnd);

    // Escape cancels any in-progress render, as an escape hatch for a
    // shape that's too expensive to mesh at the current resolution/bounds
    // (previously the only way out of a long render was to kill the application
    // entirely, losing unsaved work).
    auto cancel_render = new QAction("Cancel render", this);
    Shortcuts::add(cancel_render, "view.cancel-render", {QKeySequence(Qt::Key_Escape)});
    addAction(cancel_render);
    connect(cancel_render, &QAction::triggered, this, &Window::onCancelRender);

    // Autosave every 5 seconds if there are unsaved changes and we have
    // a real file to save to (there was no autosave at first, so a
    // frozen/force-closed session could lose all unsaved work).
    autosaveTimer.setInterval(5000);
    connect(&autosaveTimer, &QTimer::timeout, this, &Window::onAutosave);
    autosaveTimer.start();

    // File menu
    auto file_menu = menuBar()->addMenu("&File");

    auto new_action = file_menu->addAction("New");
    Shortcuts::add(new_action, "file.new", QKeySequence::keyBindings(QKeySequence::New));
    connect(new_action, &QAction::triggered, this, &Window::onNew);

    auto open_action = file_menu->addAction("Open...");
    Shortcuts::add(open_action, "file.open", QKeySequence::keyBindings(QKeySequence::Open));
    connect(open_action, &QAction::triggered, this, &Window::onOpen);

    recent_menu = file_menu->addMenu("Open recent");
    updateRecentMenu();

    // The examples are small scripts that show how the features work: they have their own entry in the menu
    auto example_action = file_menu->addAction("Open example file...");
    Shortcuts::add(example_action, "file.open-example", {});
    connect(example_action, &QAction::triggered, this, &Window::onOpenExample);

    auto import_action = file_menu->addAction("Import model...");
    Shortcuts::add(import_action, "file.import-model", {QKeySequence(Qt::CTRL | Qt::Key_I)});
    connect(import_action, &QAction::triggered, this, &Window::onImportModel);

    auto open_viewer = file_menu->addAction("Open as viewer...");
    Shortcuts::add(open_viewer, "file.open-viewer", {});
    connect(open_viewer, &QAction::triggered, this, &Window::onOpenViewer);

    // Add a "Revert to saved" item, which is only enabled if there are
    // unsaved changes and there's an existing filename to load from.
    auto default_action = file_menu->addAction("Load the default script");
    connect(default_action, &QAction::triggered, this, &Window::onLoadDefault);

    auto revert_action = file_menu->addAction("Revert to saved");
    Shortcuts::add(revert_action, "file.revert", {});
    connect(revert_action, &QAction::triggered, this, &Window::onRevert);
    connect(editor, &Editor::modificationChanged,
            revert_action, [=](bool changed){
                revert_action->setEnabled(
                        changed && !this->filename.isEmpty()); });
    revert_action->setEnabled(false);

    file_menu->addSeparator();

    auto save_action = file_menu->addAction("Save");
    Shortcuts::add(save_action, "file.save", QKeySequence::keyBindings(QKeySequence::Save));
    connect(save_action, &QAction::triggered, this, &Window::onSave);

    auto save_as_action = file_menu->addAction("Save As...");
    Shortcuts::add(save_as_action, "file.save-as", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)});
    connect(save_as_action, &QAction::triggered, this, &Window::onSaveAs);

    file_menu->addSeparator();

    auto export_action = file_menu->addAction("Export STL...");
    Shortcuts::add(export_action, "file.export-stl",
                   {QKeySequence(Qt::CTRL | Qt::Key_E), QKeySequence(Qt::Key_F7)});
    connect(export_action, &QAction::triggered, this, &Window::onExport);

    auto shot_action = file_menu->addAction("Export screenshot...");
    Shortcuts::add(shot_action, "file.export-screenshot", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)});
    connect(shot_action, &QAction::triggered, this, [this]{
        const QImage img = view->grabFramebuffer();
        QString f = QFileDialog::getSaveFileName(this, "Export screenshot",
                workingDirectory(), "PNG image (*.png);;JPEG image (*.jpg)");
        if (f.isEmpty()) return;
        if (QFileInfo(f).suffix().isEmpty()) f += ".png";
        if (img.save(f))
        {
            statusBar()->showMessage("Screenshot saved to " + QDir::toNativeSeparators(f), 6000);
        }
        else
        {
            QMessageBox::warning(this, "FielDes", "Could not save the screenshot to\n" + f);
        }
    });
    auto copy_shot = file_menu->addAction("Copy screenshot to clipboard");
    Shortcuts::add(copy_shot, "file.copy-screenshot", {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C)});
    connect(copy_shot, &QAction::triggered, this, [this]{
        QGuiApplication::clipboard()->setImage(view->grabFramebuffer());
        statusBar()->showMessage("Screenshot copied to the clipboard", 4000);
    });

    file_menu->addSeparator();

    auto quit_action = file_menu->addAction("Quit");
    Shortcuts::add(quit_action, "file.quit", {QKeySequence(Qt::CTRL | Qt::Key_Q)});
    connect(quit_action, &QAction::triggered, this, &Window::onQuit);

    // Settings menu: built here, added to the menu bar after View (the things the user sets once)
    auto settings_menu = new QMenu("&Settings", this);
    {
        auto shortcuts_action = settings_menu->addAction("Keyboard shortcuts...");
        Shortcuts::add(shortcuts_action, "settings.keyboard-shortcuts",
                       {QKeySequence(Qt::CTRL | Qt::Key_K, Qt::CTRL | Qt::Key_S)});
        connect(shortcuts_action, &QAction::triggered, this, [this]{
            ShortcutDialog d(this);
            d.exec();
        });
        settings_menu->addSeparator();
        // The custom blocks: the files of one folder become functions in every script
        auto blocks_action = settings_menu->addAction("Blocks folder...");
        blocks_action->setToolTip("The folder whose Python files are your custom blocks: every function in them is there in every script");
        connect(blocks_action, &QAction::triggered, this, [this]{ chooseBlocksFolder(); });
        auto show_blocks = settings_menu->addAction("Show the blocks folder");
        connect(show_blocks, &QAction::triggered, this, [this]{
            if (m_blocksFolder.isEmpty()) watchBlocks();
            QDir().mkpath(m_blocksFolder);
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_blocksFolder));
        });
        settings_menu->addSeparator();
    }

    // Edit menu
    auto edit_menu = menuBar()->addMenu("&Edit");
    auto undo_action = edit_menu->addAction("Undo");
    undo_action->setEnabled(false);
    Shortcuts::add(undo_action, "edit.undo", QKeySequence::keyBindings(QKeySequence::Undo));
    connect(undo_action, &QAction::triggered, editor, &Editor::undo);
    connect(editor, &Editor::undoAvailable, undo_action, &QAction::setEnabled);
    connect(editor, &Editor::notice, this, [this](const QString& text) { statusBar()->showMessage(text, 6000); });

    auto redo_action = edit_menu->addAction("Redo");
    redo_action->setEnabled(false);
    Shortcuts::add(redo_action, "edit.redo", QKeySequence::keyBindings(QKeySequence::Redo));
    connect(redo_action, &QAction::triggered, editor, &Editor::redo);
    connect(editor, &Editor::redoAvailable, redo_action, &QAction::setEnabled);

    edit_menu->addSeparator();

    {   // IDE commands, all acting on the script editor
        // (the tab being edited: the script, or a file opened from it; looked up when the command runs)
        auto script = [this]{ return editor->scriptWidget(); };
        auto find_bar = [this]{ return editor->findBar(); };
        auto cmd = [&](QMenu* m, const QString& id, const QString& text,
                       QList<QKeySequence> keys, auto&& fn) {
            auto a = m->addAction(text);
            Shortcuts::add(a, id, keys);
            connect(a, &QAction::triggered, this, fn);
            return a;
        };
        auto find_menu = edit_menu->addMenu("Find");
        cmd(find_menu, "edit.find", "Find...", {QKeySequence(Qt::CTRL | Qt::Key_F)},
            [=]{ find_bar()->showFind(); });
        cmd(find_menu, "edit.replace", "Find and replace...", {QKeySequence(Qt::CTRL | Qt::Key_H)},
            [=]{ find_bar()->showReplace(); });
        cmd(find_menu, "edit.find-next", "Find next", {QKeySequence(Qt::Key_F3)},
            [=]{ find_bar()->findNext(); });
        cmd(find_menu, "edit.find-previous", "Find previous", {QKeySequence(Qt::SHIFT | Qt::Key_F3)},
            [=]{ find_bar()->findPrevious(); });
        cmd(find_menu, "edit.select-next-occurrence", "Add next occurrence to selection (multi-cursor); duplicate the selected models",
            {QKeySequence(Qt::CTRL | Qt::Key_D)}, [=]{
                // (the same key in the viewport and the model tree: a copy of every selected model under it)
                QWidget* f = QApplication::focusWidget();
                if (f && view && (f == view || view->isAncestorOf(f)) && !qobject_cast<QLineEdit*>(f))
                    view->scenePanel()->duplicateSelected();
                else
                    script()->selectNextOccurrence();
            });

        auto nav_menu = edit_menu->addMenu("Go to");
        cmd(nav_menu, "edit.go-to-definition", "Go to definition", {QKeySequence(Qt::Key_F12)},
            [=]{ script()->goToDefinitionAtCursor(); });
        cmd(nav_menu, "edit.go-to-line", "Go to line...", {QKeySequence(Qt::CTRL | Qt::Key_G)},
            [=]{ script()->promptGoToLine(); });
        nav_menu->addSeparator();
        cmd(nav_menu, "edit.back-to-script", "Back to the rendered script", {QKeySequence(Qt::ALT | Qt::Key_Left)},
            [=]{ editor->showScriptTab(); editor->scriptWidget()->setFocus(); });
        cmd(nav_menu, "edit.next-tab", "Next tab", {QKeySequence(Qt::CTRL | Qt::Key_PageDown)},
            [=]{ editor->nextTab(); });
        cmd(nav_menu, "edit.previous-tab", "Previous tab", {QKeySequence(Qt::CTRL | Qt::Key_PageUp)},
            [=]{ editor->previousTab(); });
        cmd(nav_menu, "edit.close-tab", "Close tab", {QKeySequence(Qt::CTRL | Qt::Key_W)},
            [=]{ editor->closeCurrentTab(); });

        auto lines_menu = edit_menu->addMenu("Lines");
        cmd(lines_menu, "edit.toggle-comment", "Toggle comment", {QKeySequence(Qt::CTRL | Qt::Key_Slash)},
            [=]{ script()->toggleComment(); });
        cmd(lines_menu, "edit.duplicate-lines", "Duplicate line(s)",
            {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D)}, [=]{ script()->duplicateLines(); });
        cmd(lines_menu, "edit.delete-lines", "Delete line(s)",
            {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K)}, [=]{ script()->deleteLines(); });
        cmd(lines_menu, "edit.move-lines-up", "Move line(s) up", {QKeySequence(Qt::ALT | Qt::Key_Up)},
            [=]{ script()->moveLinesUp(); });
        cmd(lines_menu, "edit.move-lines-down", "Move line(s) down", {QKeySequence(Qt::ALT | Qt::Key_Down)},
            [=]{ script()->moveLinesDown(); });

        auto break_menu = edit_menu->addMenu("Breakpoints");
        cmd(break_menu, "edit.toggle-breakpoint", "Toggle breakpoint on this line",
            {QKeySequence(Qt::Key_F9)}, [=]{ script()->toggleBreakpointAtCursor(); });
        cmd(break_menu, "edit.continue", "Continue from the breakpoint",
            {QKeySequence(Qt::Key_F8)}, [=]{ editor->continueRun(); });
        cmd(break_menu, "edit.clear-breakpoints", "Remove all breakpoints", {},
            [=]{ script()->clearBreakpoints(); });

        auto fold_menu = edit_menu->addMenu("Folding");
        cmd(fold_menu, "edit.fold-toggle", "Fold / unfold at cursor",
            {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft)}, [=]{ script()->toggleFoldAtCursor(); });
        cmd(fold_menu, "edit.fold-all", "Fold all",
            {QKeySequence(Qt::CTRL | Qt::Key_K, Qt::CTRL | Qt::Key_0)}, [=]{ script()->foldAll(); });
        cmd(fold_menu, "edit.unfold-all", "Unfold all",
            {QKeySequence(Qt::CTRL | Qt::Key_K, Qt::CTRL | Qt::Key_J)}, [=]{ script()->unfoldAll(); });

        cmd(edit_menu, "edit.complete", "Autocomplete", {QKeySequence(Qt::CTRL | Qt::Key_Space)},
            [=]{ script()->setFocus(); script()->triggerCompletion(); });
        // (the text size is a setting: Ctrl++ and Ctrl+-, also on the numeric keypad)
        cmd(settings_menu, "settings.font-bigger", "Editor text bigger",
            {QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::KeypadModifier | Qt::Key_Plus)},
            [=]{ script()->zoomInFont(); });
        cmd(settings_menu, "settings.font-smaller", "Editor text smaller",
            {QKeySequence(Qt::CTRL | Qt::Key_Minus), QKeySequence(Qt::CTRL | Qt::KeypadModifier | Qt::Key_Minus)},
            [=]{ script()->zoomOutFont(); });
    }

    auto autoload_action = settings_menu->addAction("Automatically reload changes");
    connect(autoload_action, &QAction::triggered, this,
            [&](bool b) { autoreload = b; });
    autoload_action->setCheckable(true);

    connect(this, &Window::setAutoload, autoload_action,
            [=](bool b) { autoload_action->setChecked(b);
                          this->autoreload = b; });

    settings_menu->addSeparator();

    // View menu
    auto view_menu = menuBar()->addMenu("&View");
    auto show_axes_action = view_menu->addAction("Show origin axes");
    show_axes_action->setCheckable(true);
    connect(show_axes_action, &QAction::toggled, [this](bool b){
        view->showAxes(b);
        settings.setValue("show-axes", b);
    });
    show_axes_action->setChecked(settings.value("show-axes", true).toBool());

    auto show_triad_action = view_menu->addAction("Show orientation triad");
    show_triad_action->setCheckable(true);
    connect(show_triad_action, &QAction::toggled, [this](bool b){
        view->showTriad(b);
        settings.setValue("show-triad", b);
    });
    show_triad_action->setChecked(settings.value("show-triad", true).toBool());

    auto show_legends_action = view_menu->addAction("Show legends");
    show_legends_action->setCheckable(true);
    connect(show_legends_action, &QAction::toggled, [this](bool b){
        view->showLegends(b);
        settings.setValue("show-legends", b);
    });
    show_legends_action->setChecked(settings.value("show-legends", true).toBool());

    auto show_bbox_action = view_menu->addAction("Show bounding box(es)");
    show_bbox_action->setCheckable(true);
    connect(show_bbox_action, &QAction::toggled, [this](bool b) {
        view->showBBox(b);
        settings.setValue("show-bounding-box", b);
    });
    show_bbox_action->setChecked(settings.value("show-bounding-box", false).toBool());

    auto perspective_action = new QAction("Perspective", nullptr);
    auto ortho_action = new QAction("Orthographic", nullptr);
    auto proj_menu = new QMenu("Projection");
    proj_menu->addAction(perspective_action);
    proj_menu->addAction(ortho_action);
    perspective_action->setCheckable(true);
    ortho_action->setCheckable(true);
    auto projection = new QActionGroup(proj_menu);
    projection->addAction(perspective_action);
    projection->addAction(ortho_action);
    connect(perspective_action, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->toPerspective();
        settings.setValue("projection", "perspective");
    });
    connect(ortho_action, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->toOrthographic();
        settings.setValue("projection", "orthographic");
    });
    view_menu->addMenu(proj_menu);
    if (settings.value("projection", "").toString() == "orthographic")
        ortho_action->setChecked(true);
    else
        perspective_action->setChecked(true);

    auto turn_y_up = new QAction("Turntable (Y up)", nullptr);
    auto turn_z_up = new QAction("Turntable (Z up)", nullptr);
    auto rotation_menu = new QMenu("Rotation mode");
    rotation_menu->addAction(turn_y_up);
    rotation_menu->addAction(turn_z_up);
    turn_z_up->setCheckable(true);
    turn_y_up->setCheckable(true);
    auto rot_mode = new QActionGroup(rotation_menu);
    rot_mode->addAction(turn_y_up);
    rot_mode->addAction(turn_z_up);
    // (the Y axis points up unless the user chose otherwise: the choice is stored under its own key, only
    // when it is made -- the old "rotation" key was written at every start, so it says nothing about a choice)
    connect(turn_z_up, &QAction::triggered, [this](bool) {
        view->toTurnZ();
        settings.setValue("up-axis", "z");
    });
    connect(turn_y_up, &QAction::triggered, [this](bool) {
        view->toTurnY();
        settings.setValue("up-axis", "y");
    });
    settings_menu->addMenu(rotation_menu);
    const bool y_is_up = settings.value("up-axis", "y").toString() != "z";
    (y_is_up ? turn_y_up : turn_z_up)->setChecked(true);
    view->setUpAxis(y_is_up);

    auto sensitivity_low = new QAction("Low", nullptr);
    auto sensitivity_medium = new QAction("Medium", nullptr);
    auto sensitivity_high = new QAction("High", nullptr);
    auto sensitivity_menu = new QMenu("Rotation sensitivity");
    sensitivity_menu->addAction(sensitivity_low);
    sensitivity_menu->addAction(sensitivity_medium);
    sensitivity_menu->addAction(sensitivity_high);
    sensitivity_low->setCheckable(true);
    sensitivity_medium->setCheckable(true);
    sensitivity_high->setCheckable(true);
    auto sense_mode = new QActionGroup(sensitivity_menu);
    sense_mode->addAction(sensitivity_low);
    sense_mode->addAction(sensitivity_medium);
    sense_mode->addAction(sensitivity_high);
    connect(sensitivity_low, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->setLowRotSensitivity();
        settings.setValue("rotation-sensitvity", "low");
    });
    connect(sensitivity_medium, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->setMedRotSensitivity();
        settings.setValue("rotation-sensitvity", "medium");
    });
    connect(sensitivity_high, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->setHighRotSensitivity();
        settings.setValue("rotation-sensitvity", "high");
    });
    settings_menu->addMenu(sensitivity_menu);
    QString rotation_setting = settings.value("rotation-sensitvity", "").toString();
    if (rotation_setting == "low")
        sensitivity_low->setChecked(true);
    else if (rotation_setting == "high")
        sensitivity_high->setChecked(true);
    else
        sensitivity_medium->setChecked(true);

    auto cursor_centric = new QAction("Cursor", nullptr);
    auto scene_centric = new QAction("Scene", nullptr);
    auto zoom_menu = new QMenu("Zoom center");
    zoom_menu->addAction(cursor_centric);
    zoom_menu->addAction(scene_centric);
    cursor_centric->setCheckable(true);
    scene_centric->setCheckable(true);
    auto zoom_mode = new QActionGroup(zoom_menu);
    zoom_mode->addAction(cursor_centric);
    zoom_mode->addAction(scene_centric);
    connect(cursor_centric, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->setZoomCursorCentric();
        settings.setValue("zoom-center", "cursor");
    });
    connect(scene_centric, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->setZoomSceneCentric();
        settings.setValue("zoom-center", "scene");
    });
    settings_menu->addMenu(zoom_menu);
    if (settings.value("zoom-center", "").toString() == "scene")
        scene_centric->setChecked(true);
    else
        cursor_centric->setChecked(true);

    view_menu->addSeparator();
    auto tree_action = view_menu->addAction("Model tree");
    tree_action->setCheckable(true);
    Shortcuts::add(tree_action, "view.model-tree", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T)});
    connect(tree_action, &QAction::toggled, this, [this](bool b) {
        view->scenePanel()->setVisible(b);
        settings.setValue("show-model-tree", b);
    });
    tree_action->setChecked(settings.value("show-model-tree", true).toBool());
    view->scenePanel()->setVisible(tree_action->isChecked());

    QAction* sectionToggle = nullptr;
    {   // Section view / field viewer: a card floating in the viewport
        auto panel = new SectionPanel(view);
        panel->hide();

        connect(panel, &SectionPanel::settingsChanged, view, &View::setSection);
        panel->setModelBoundsProvider([this](QVector3D& lo, QVector3D& hi) {
            return view->meshBounds(lo, hi);
        });
        panel->setCameraDirectionProvider([this]{ return view->towardViewer(); });
        // The field viewer: a card of its own, like the section card, for the field that is selected (a disc, no cut).  The two
        // views are separate: the section view is switched on by the user and cuts models, the field viewer is there while a
        // field is selected; each has its own plane in the viewport, and each card hears only its own plane
        auto fieldPanel = new FieldPanel(view);
        fieldPanel->hide();
        fieldPanel->stackBelow(panel);
        connect(fieldPanel, &FieldPanel::settingsChanged, view, &View::setFieldView);
        connect(view, &View::fieldCentreDragged, fieldPanel, &FieldPanel::setCentre);
        connect(view, &View::sliceReady, panel, &SectionPanel::setSlice);
        connect(view, &View::fieldSliceReady, fieldPanel, &FieldPanel::setSlice);
        connect(view, &View::boundsChanged, panel, &SectionPanel::setBounds);
        connect(view, &View::sectionOffsetDragged, panel, &SectionPanel::setOffset);
        connect(view, &View::sectionReadout, panel, &SectionPanel::setReadout);
        connect(view, &View::fieldReadout, fieldPanel, &FieldPanel::setReadout);
        connect(view, &View::sectionCutsElements, panel, &SectionPanel::setWholeElementsAvailable);
        if (automated)
        {
            connect(view, &View::sectionReadout, this, [](QString t) {
                std::cerr << "automation: readout '" << t.toStdString() << "'" << std::endl;
            });
            connect(view, &View::fieldReadout, this, [](QString t) {
                std::cerr << "automation: field readout '" << t.toStdString() << "'" << std::endl;
            });
        }

        auto section_action = view_menu->addAction("Section view");
        section_action->setCheckable(true);
        sectionToggle = section_action;
        Shortcuts::add(section_action, "view.section", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X)});
        // Selecting field models (they are not drawn) opens the field viewer on them, which shows the field on a disc; with several
        // fields selected a menu in it chooses the one shown.  It closes when no field is selected.  The section card is not touched
        auto shownKey = std::make_shared<QString>();
        auto showKey = [=](const QString& key, const QStringList& keys) {
            *shownKey = key;
            QStringList names;
            for (const QString& k : keys) names << (k.startsWith("line:") ? "field at line " + k.mid(5) : k);
            view->showField(key);
            fieldPanel->setFields(keys, names, std::max(0, int(keys.indexOf(key))));
            bool known = false;
            const QVector3D centre = view->fieldCentre(&known);
            const auto& s = view->renderSettings();
            fieldPanel->setContext(centre, known, s.min, s.max);
            fieldPanel->show();
            fieldPanel->raise();
        };
        auto fieldKeys = std::make_shared<QStringList>();
        connect(view->scenePanel(), &ScenePanel::fieldsSelected, this, [=](QStringList keys) {
            *fieldKeys = keys;
            if (keys.isEmpty())
            {
                *shownKey = QString();
                view->showField(QString());
                fieldPanel->hide();
                return;
            }
            showKey(keys.contains(*shownKey) ? *shownKey : keys.first(), keys);
        });
        connect(fieldPanel, &FieldPanel::fieldChosen, this, [=](QString key) { showKey(key, *fieldKeys); });        connect(editor, &Editor::fieldSources, view, &View::setFieldSources);
        connect(section_action, &QAction::toggled, this, [=](bool on) {
            if (on)
            {
                const auto& s = view->renderSettings();
                panel->setBounds(s.min, s.max);
                panel->show();
                panel->raise();
            }
            panel->setEnabledSection(on);
            if (!on) panel->hide();
        });
        connect(panel, &SectionPanel::closeRequested, section_action, [=]{
            section_action->setChecked(false);
        });
    }

    {   // Standard views; single keys, so they only act while the viewport
        // has keyboard focus (click it first) and never eat typing
        auto std_menu = view_menu->addMenu("Standard views");
        const int KP = int(Qt::KeypadModifier);
        struct V { const char* id; const char* name; int which; QList<QKeySequence> keys; };
        const QList<V> views = {
            {"view.std-front",  "Front",     View::VIEW_FRONT,  {QKeySequence(Qt::Key_1), QKeySequence(KP | Qt::Key_1)}},
            {"view.std-back",   "Back",      View::VIEW_BACK,   {QKeySequence(Qt::CTRL | Qt::Key_1), QKeySequence(Qt::CTRL | KP | Qt::Key_1)}},
            {"view.std-right",  "Right",     View::VIEW_RIGHT,  {QKeySequence(Qt::Key_3), QKeySequence(KP | Qt::Key_3)}},
            {"view.std-left",   "Left",      View::VIEW_LEFT,   {QKeySequence(Qt::CTRL | Qt::Key_3), QKeySequence(Qt::CTRL | KP | Qt::Key_3)}},
            {"view.std-top",    "Top",       View::VIEW_TOP,    {QKeySequence(Qt::Key_7), QKeySequence(KP | Qt::Key_7)}},
            {"view.std-bottom", "Bottom",    View::VIEW_BOTTOM, {QKeySequence(Qt::CTRL | Qt::Key_7), QKeySequence(Qt::CTRL | KP | Qt::Key_7)}},
            {"view.std-iso",    "Isometric", View::VIEW_ISO,    {QKeySequence(Qt::Key_0), QKeySequence(KP | Qt::Key_0)}},
        };
        for (const auto& v : views)
        {
            auto a = std_menu->addAction(v.name);
            Shortcuts::add(a, v.id, v.keys);
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            view->addAction(a);
            const int which = v.which;
            connect(a, &QAction::triggered, view, [=]{ view->standardView(which); });
        }
    }

    {   // How the selected models are edited by dragging: single keys, active while the viewport (or the
        // model tree) has the keyboard focus, so they never eat typing.  E goes round when the gizmo is shown (click,
        // never, always); R locks or unlocks, a switch of its own that leaves the gizmo mode alone
        auto mode_menu = view_menu->addMenu("Edit mode");
        struct E { const char* id; const char* name; const char* mode; Qt::Key key; };
        const QList<E> modes = {
            {"view.edit-toggle",  "Gizmo: click / never / always", "toggle", Qt::Key_E},
            {"view.edit-lock",    "Lock / unlock",          "lock",    Qt::Key_R},
            {"view.visible",      "Show / hide",            "visible", Qt::Key_V},
            {"view.cache",        "Render cache on / off",  "cache",   Qt::Key_C},
        };
        for (const auto& m : modes)
        {
            auto a = mode_menu->addAction(m.name);
            Shortcuts::add(a, m.id, {QKeySequence(m.key)});
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            view->addAction(a);
            const QString mode = m.mode;
            auto scene = view->scenePanel();
            if (mode == "toggle") connect(a, &QAction::triggered, scene, [=]{ scene->toggleSelectedEdit(); });
            else if (mode == "lock") connect(a, &QAction::triggered, scene, [=]{ scene->toggleSelectedLock(); });
            else if (mode == "visible") connect(a, &QAction::triggered, scene, [=]{ scene->toggleSelectedVisible(); });
            else if (mode == "cache") connect(a, &QAction::triggered, scene, [=]{ scene->toggleSelectedCache(); });
        }
    }

    {   // Isolation: a single key like the edit modes, so it never eats typing in the editor
        auto isolate = view_menu->addAction("Isolate the selected model");
        Shortcuts::add(isolate, "view.isolate", {QKeySequence(Qt::Key_I)});
        isolate->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(isolate);
        auto scene = view->scenePanel();
        connect(isolate, &QAction::triggered, scene, &ScenePanel::toggleIsolation);
    }

    {   // Delete: D, a single key like the others (the viewport or the model tree has the focus, so it never eats typing)
        auto del = view_menu->addAction("Delete the selected models");
        Shortcuts::add(del, "view.delete", {QKeySequence(Qt::Key_D)});
        del->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(del);
        auto scene = view->scenePanel();
        connect(del, &QAction::triggered, scene, &ScenePanel::deleteSelected);
    }

    {   // Copy and paste of models: Ctrl+C and Ctrl+V with the viewport or the model tree focused (a text field of the
        // tree keeps its own copy and paste)
        auto scene = view->scenePanel();
        auto copy = view_menu->addAction("Copy the selected models");
        Shortcuts::add(copy, "view.copy-models", {QKeySequence(Qt::CTRL | Qt::Key_C)});
        copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(copy);
        connect(copy, &QAction::triggered, scene, [scene] {
            if (auto field = qobject_cast<QLineEdit*>(QApplication::focusWidget())) field->copy();
            else scene->copySelected();
        });
        auto paste = view_menu->addAction("Paste the copied models");
        Shortcuts::add(paste, "view.paste-models", {QKeySequence(Qt::CTRL | Qt::Key_V)});
        paste->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(paste);
        connect(paste, &QAction::triggered, scene, [scene] {
            if (auto field = qobject_cast<QLineEdit*>(QApplication::focusWidget())) field->paste();
            else scene->pasteModels();
        });
    }

    auto frame_all = view_menu->addAction("Frame all shapes");
    Shortcuts::add(frame_all, "view.frame-all", {QKeySequence(Qt::Key_Home)});
    connect(frame_all, &QAction::triggered, view, &View::frameAll);

    auto zoom_to_action = new QAction("Zoom to bounds", nullptr);
    view_menu->addAction(zoom_to_action);
    Shortcuts::add(zoom_to_action, "view.zoom-to-bounds", {});
    connect(zoom_to_action, &QAction::triggered, view, &View::zoomTo);

    // How the viewport meshes (the render options): the last part of the View menu
    view_menu->addSeparator();
    auto dc_meshing = new QAction("Dual contouring", nullptr);
    auto iso_meshing = new QAction("Iso-simplex", nullptr);
    auto hybrid_meshing = new QAction("Hybrid", nullptr);
    auto meshing_menu = new QMenu("Meshing algorithm");
    auto meshing_mode = new QActionGroup(meshing_menu);
    view_menu->addMenu(meshing_menu);

    for (auto& m : { dc_meshing, iso_meshing, hybrid_meshing }) {
        meshing_menu->addAction(m);
        meshing_mode->addAction(m);
        m->setCheckable(true);
    }
    connect(dc_meshing, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->toDCMeshing();
        settings.setValue("meshing-algorithm", "dual-contouring");
    });
    connect(iso_meshing, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->toIsoMeshing();
        settings.setValue("meshing-algorithm", "iso-simplex");
    });
    connect(hybrid_meshing, &QAction::toggled, [this](bool b) {
        if (!b) return;
        view->toHybridMeshing();
        settings.setValue("meshing-algorithm", "hybrid");
    });
    QString algorithm_setting = settings.value("meshing-algorithm", "").toString();
    if (algorithm_setting == "iso-simplex")
        iso_meshing->setChecked(true);
    else if (algorithm_setting == "hybrid")
        hybrid_meshing->setChecked(true);
    else
        dc_meshing->setChecked(true);

    // The render cache (the model tree's cache button keeps a shape's mesh): everything it kept
    settings_menu->addSeparator();
    auto clear_cache = settings_menu->addAction("Clear the caches");
    clear_cache->setToolTip("Delete every mesh the render cache kept and every field the field cache kept (they are computed again the next "
                            "time they are needed, and kept again)");
    connect(clear_cache, &QAction::triggered, this, [this]{
        qint64 bytes = 0;
        for (const auto& f : QDir(Shape::renderCacheDir()).entryInfoList({"*.fdmesh"}, QDir::Files))
            bytes += f.size();
        const int n = Shape::clearRenderCache();
        // the field cache (field_cache.py) sits beside the render cache: a file or two to a field
        int fields = 0;
        const QDir fieldDir(QFileInfo(Shape::renderCacheDir()).absolutePath() + "/field-cache");
        for (const auto& f : fieldDir.entryInfoList({"*.fdtree", "*.fdfield", "*.part"}, QDir::Files))
        {
            bytes += f.size();
            if (f.suffix() == "fdfield") ++fields;
            QFile::remove(f.absoluteFilePath());
        }
        statusBar()->showMessage(QString("Caches cleared: %1 kept mesh(es) and %2 kept field(s), %3 MB deleted")
                                     .arg(n).arg(fields).arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1), 8000);
    });

    // The messages that were hidden with "Do not show this again" come back
    settings_menu->addSeparator();
    auto show_messages = settings_menu->addAction("Show hidden messages again");
    show_messages->setToolTip("The messages you turned off with \"Do not show this message again\" are shown again");
    connect(show_messages, &QAction::triggered, this, [this]{
        QSettings().remove("hidden-messages");
        statusBar()->showMessage("Hidden messages will be shown again", 6000);
    });

    menuBar()->addMenu(settings_menu);

    // Help menu
    auto help_menu = menuBar()->addMenu("Help");
    auto guide_action = help_menu->addAction("FielDes guide (features and shortcuts)");
    Shortcuts::add(guide_action, "help.guide", {QKeySequence(Qt::SHIFT | Qt::Key_F1)});
    connect(guide_action, &QAction::triggered, this, [this]{
        auto d = new QDialog(this);
        d->setObjectName("FielDesGuide");
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->setWindowTitle("FielDes guide");
        auto text = new QTextBrowser;
        text->setOpenExternalLinks(true);
        text->setHtml(guideHtml());
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::close);
        auto layout = new QVBoxLayout(d);
        layout->addWidget(text);
        layout->addWidget(buttons);
        d->resize(760, 720);
        d->show();
    });
    help_menu->addSeparator();
    connect(help_menu->addAction("About"), &QAction::triggered,
            this, &Window::onAbout);
    connect(help_menu->addAction("Guided tour"), &QAction::triggered, this, [this]{ if (tour) tour->start(); });
    auto ref_action = help_menu->addAction("Shape reference");
    Shortcuts::add(ref_action, "help.shape-reference", {QKeySequence(Qt::Key_F1)});
    connect(ref_action, &QAction::triggered, editor, &Editor::onShowDocs);

    {   // The top dock: New (a plus), Open and Import as large icons at the very left, then the menus
        auto holder = new QWidget;
        holder->setObjectName("TopIcons");
        auto row = new QHBoxLayout(holder);
        row->setContentsMargins(6, 0, 6, 0);
        row->setSpacing(2);
        auto add = [&](QAction* target, const QIcon& icon, const QString& what, bool imports, bool recent = true) {
            auto b = new QToolButton;
            b->setObjectName("TopButton");
            b->setIcon(icon);
            b->setIconSize(QSize(23, 23));
            b->setFixedSize(30, 30);
            b->setAutoRaise(true);
            b->setCursor(Qt::PointingHandCursor);
            // (the tip names the key, and follows it when the key is rebound)
            auto tip = [=]{
                const QString k = target->shortcut().toString(QKeySequence::NativeText);
                b->setToolTip(k.isEmpty() ? what : what + "  (" + k + ")");
            };
            tip();
            connect(target, &QAction::changed, b, tip);
            connect(b, &QToolButton::clicked, target, &QAction::trigger);
            row->addWidget(b);
            if (!recent)
            {
                // (a plus has no list of files behind it, so no arrow: the room the arrow takes is left empty, 14 wide plus
                //  the gap either side of it, so the icons after it are spaced like the ones after Open)
                row->addSpacing(14 + row->spacing() + 4);
                return;
            }
            // A small arrow beside it: the files opened (imported) lately, to choose one
            auto arrow = new QToolButton;
            arrow->setObjectName("TopArrow");
            arrow->setIcon(Icons::dropArrow());
            arrow->setIconSize(QSize(11, 11));
            arrow->setFixedSize(14, 30);
            arrow->setAutoRaise(true);
            arrow->setCursor(Qt::PointingHandCursor);
            arrow->setToolTip(imports ? "Recently imported models" : "Recently opened scripts");
            connect(arrow, &QToolButton::clicked, this, [=]{ showRecent(arrow, imports); });
            row->addWidget(arrow);
            row->addSpacing(imports ? 0 : 4);
        };
        add(new_action, Icons::newFile(), "New script", false, false);
        add(open_action, Icons::open(), "Open a script", false);
        add(import_action, Icons::importFile(), "Import a model", true);
        auto line = new QFrame;
        line->setObjectName("TopDivider");
        line->setFrameShape(QFrame::VLine);
        line->setFixedHeight(18);
        row->addSpacing(4);
        row->addWidget(line);
        menuBar()->setCornerWidget(holder, Qt::TopLeftCorner);
    }

    // Link up the editor and the view
    connect(editor, &Editor::shapes, view, &View::setShapes);
    connect(view, &View::varsDragged, editor, &Editor::setVarValues);

    // Model tree: every action is an edit of the script
    {
        auto scene = view->scenePanel();
        scene->setScriptSource([this]{ return editor->getScript(); });
        // (the statement that makes a shape's surfaces draggable is written by the interpreter, which has the shape)
        scene->setExposeSource([this](const QString& var, QString* error) {
            return editor->callSupport("expose_text", var, error);
        });
        connect(editor, &Editor::sceneChanged, scene, &ScenePanel::setScene);
        connect(editor, &Editor::partialSceneChanged, scene, &ScenePanel::setPartialScene);
        connect(editor, &Editor::documentReplaced, scene, &ScenePanel::clearScene);
        connect(editor, &Editor::scriptLoaded, scene, &ScenePanel::showCached);
        connect(editor, &Editor::sceneChanged, this, &Window::onSceneChanged);

        // An error in the script, shown where the work is (a banner and a frame in the viewport, the model tree's card) -- not at
        // once: a line that is being typed is wrong for a while, and a banner at every key would be noise.  It appears when the
        // script has been wrong for a moment with nothing typed, goes the moment the script runs well, and while it is there
        // the message follows the script
        {
            struct ErrorState { QString text; int line = -1; bool shown = false; };
            auto error = std::make_shared<ErrorState>();
            auto delay = new QTimer(this);
            delay->setSingleShot(true);
            delay->setInterval(1300);
            auto show = [=](bool on) {
                error->shown = on;
                view->setError(on ? error->text : QString(), on ? error->line : -1);
                scene->setError(on ? error->text : QString(), on ? error->line : -1);
            };
            connect(delay, &QTimer::timeout, this, [=] { if (!error->text.isEmpty()) show(true); });
            connect(editor, &Editor::scriptErrorChanged, this, [=](QString text, int line0) {
                error->text = text;
                error->line = line0;
                if (text.isEmpty())
                {
                    delay->stop();
                    if (error->shown) show(false);
                    else { view->setError(QString(), -1); scene->setError(QString(), -1); }
                }
                else if (error->shown) show(true);          // (it is there: the message follows the script)
                else delay->start();
            });
            connect(editor->scriptWidget(), &QPlainTextEdit::textChanged, this, [=] {
                if (!error->text.isEmpty() && !error->shown) delay->start();       // (typing: the moment starts again)
            });
            connect(view, &View::errorClicked, this, [=](int line0) { if (line0 >= 0) editor->goToLine(line0); });
        }
        connect(scene, &ScenePanel::goToLine, editor, &Editor::goToLine);
        connect(scene, &ScenePanel::editScript, editor, &Editor::applyEdits);
        connect(scene, &ScenePanel::editScriptLive, editor, &Editor::applyEditsLive);
        connect(scene, &ScenePanel::rerunRequested, editor, &Editor::onTextChangedDebounce);
        connect(scene, &ScenePanel::highlightLines, view, &View::highlightLines);
        connect(scene, &ScenePanel::sourceLinesMoved, view, &View::moveSourceLines);
        connect(scene, &ScenePanel::focusRequested, view, &View::focusOn);
        // (the gizmo of a model that is selected is drawn at once, before the script has run with its numbers)
        connect(scene, &ScenePanel::provisionalGizmo, view, &View::setProvisionalGizmo);
        connect(view, &View::provisionalPressed, scene, &ScenePanel::prepareNow);
        // Selecting in the viewport leaves the keyboard where it is: in the viewport, so that the keys that work on
        // the selection (E, R, G, H, I) go on working, and never in the editor
        connect(view, &View::shapeClicked, this, [=](int line) {
            // (a click on a model selects it alone; a click on empty space, without Shift or Ctrl, deselects all)
            if (line >= 0) scene->selectByLine(line);
            else scene->clearSelection();
            view->setFocus(Qt::MouseFocusReason);
        });
        connect(view, &View::shapeToggled, this, [=](int line) {
            scene->toggleByLine(line);
            view->setFocus(Qt::MouseFocusReason);
        });
        connect(view, &View::shapesRectSelected, this, [=](QList<int> lines, bool add) {
            scene->selectLines(lines, add);
            view->setFocus(Qt::MouseFocusReason);
        });
        connect(view, &View::surfaceSelectRequested, scene, &ScenePanel::addSurfaceSelection);
        // The context menus create primitives and operations: the interpreter lists them and writes the calls
        scene->setSupport([this](const QString& function, const QString& arg, QString* error) {
            return editor->callSupport(function, arg, error);
        });
        view->setMenuCatalogSource([this]{ return editor->callSupport("menu_catalog", QString(), nullptr); });
        connect(view, &View::createRequested, scene, &ScenePanel::createFromMenu);
        connect(view, &View::importRequested, this, [this] { onImportModel(); });
        // A right-click in the text editor, or in the model tree, opens the viewport's menu for the model of that line / row
        connect(editor, &Editor::objectMenuRequested, view, &View::showMenuForLine);
        connect(scene, &ScenePanel::menuRequested, view, &View::showMenuForLine);
        // (the list is asked for once, after a script has run, so that a right-click never waits for Python)
        connect(editor, &Editor::sceneChanged, view, [=]{ view->loadMenuCatalog(); });
        // (what the render cache did, on the cache buttons of the shapes that have it on)
        connect(view, &View::cacheStatesChanged, scene, [=]{ scene->setCacheStates(view->cacheStates()); });
    }

    {   // Status bar: render state on the left, region / resolution on the right
        auto region = new QLabel;
        region->setStyleSheet("color: palette(mid); padding-right: 6px;");
        statusBar()->addPermanentWidget(region);
        connect(editor, &Editor::settingsChanged, region, [=](Settings s, bool) {
            auto fmt = [](float v) { return QString::number(v, 'g', 4); };
            const QString dot = QString("   ") + QChar(0x00b7) + "   ";
            region->setText((QString("Region (%1, %2, %3) ") + QChar(0x2192) +
                QString(" (%4, %5, %6)") + dot + "resolution %7" + dot + "quality %8")
                .arg(fmt(s.min.x())).arg(fmt(s.min.y())).arg(fmt(s.min.z()))
                .arg(fmt(s.max.x())).arg(fmt(s.max.y())).arg(fmt(s.max.z()))
                .arg(fmt(s.res)).arg(fmt(s.quality)));
        });
        auto timer = new QElapsedTimer;
        auto running = new bool(false);
        connect(view, &View::renderBusy, this, [=](bool busy) {
            if (busy && !*running)
            {
                timer->start();   // (the progress bar shows the rendering)
            }
            else if (!busy && *running)
            {
                statusBar()->showMessage(QString("Rendered in %1 s")
                    .arg(timer->elapsed() / 1000.0, 0, 'f', 2), 8000);
                // (FIELDES_TIMING: also to stderr, for timing scripts)
                if (qEnvironmentVariableIsSet("FIELDES_TIMING"))
                    std::cerr << "[fieldes] rendered in " << timer->elapsed() / 1000.0 << " s\n";
            }
            *running = busy;
        });
        connect(this, &QObject::destroyed, [=]{ delete timer; delete running; });
    }

    // The window always starts maximized (an engineering tool: the model wants the room); the size it
    // had when last un-maximized, and the editor / viewport split, come back from the last session
    if (!automated)
    {
        if (settings.contains("window-geometry"))
        {
            restoreGeometry(settings.value("window-geometry").toByteArray());
            restoreState(settings.value("window-state").toByteArray());
            layout->restoreState(settings.value("splitter-state").toByteArray());
        }
    }
    setWindowState(windowState() | Qt::WindowMaximized);

    setWindowTitle("FielDes[*]");
    show();

    // The guided tour: offered the first time FielDes runs (and from Help after that)
    tour = new Tutorial(this, view, editor, [this]{ return loadTourModel(); }, this);
    connect(tour, &Tutorial::finished, this, [this]{ if (!automated) settings.setValue("tour/done", true); });
    const bool offerTour = !automated && args.filename.isEmpty() && !settings.value("tour/done", false).toBool();

    onNew();
    if (!args.filename.isEmpty() && loadFile(args.filename))
    {
        setFilename(args.filename);
    }
    if (offerTour) QTimer::singleShot(900, tour, &Tutorial::offer);

    // Scripted GUI checks (developer facility; see automation.hpp)
    const QString automation_file = qEnvironmentVariable("FIELDES_AUTOMATION");
    if (!automation_file.isEmpty())
    {
        auto a = new Automation(this, this);
        auto script = editor->scriptWidget();
        // tour <offer|start|goto N|next|back|showme|skip|state>: the guided tour, step by step; prints where it is
        a->add("tour", [=](const QString& args){
            const QString cmd = args.section(' ', 0, 0);
            const QString rest = args.section(' ', 1);
            if (cmd == "offer") tour->offer();
            else if (cmd == "start") tour->start();
            else if (cmd == "goto") tour->goTo(rest.toInt());
            else if (cmd == "next") tour->next();
            else if (cmd == "back") tour->back();
            else if (cmd == "showme") tour->showMe();
            else if (cmd == "skip") tour->stop(false);
            std::cerr << "automation: tour step " << tour->stepIndex() << "/" << tour->stepCount() - 1 << " ["
                      << tour->stepTitle().toStdString() << "] done=" << tour->stepDone() << " active=" << tour->active()
                      << " demo=" << tour->demoRunning() << "\n";
        });
        a->add("focus", [=](const QString& w){
            if (w == "view") view->setFocus(); else script->setFocus();
        });
        // waitrender [seconds [seconds to start]]: wait until the render that the script started has finished (at most
        // that long; 1800 s by default), so that a grab shows the finished picture however long it takes.  The render
        // has that many seconds (default 20) to begin after the script has run -- a script that takes longer to run
        // has to say so, or the grab is of a render that has not begun
        a->add("waitrender", [=](const QString& args){
            const auto parts = args.split(' ', Qt::SkipEmptyParts);
            const qint64 limit = (parts.isEmpty() ? 1800 : parts[0].toInt()) * 1000;
            const qint64 startWindow = (parts.size() > 1 ? parts[1].toInt() : 20) * 1000;
            QElapsedTimer total;
            total.start();
            QEventLoop loop;
            QTimer poll;
            bool started = false;
            QObject::connect(&poll, &QTimer::timeout, [&]() {
                if (view->isRendering()) started = true;
                // (a render has a moment to begin after the script has run)
                if ((started && !view->isRendering()) || (!started && total.elapsed() > startWindow) || total.elapsed() > limit)
                    loop.quit();
            });
            poll.start(200);
            loop.exec();
            std::cerr << "automation: render finished after " << total.elapsed() / 1000 << " s" << std::endl;
        });
        a->add("cursor", [=](const QString& args){
            const auto p = args.split(' ');
            if (p.size() < 2) return;
            QTextCursor c(script->document()->findBlockByNumber(p[0].toInt()));
            c.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, p[1].toInt());
            script->setTextCursor(c);
            script->setFocus();
        });
        // gutterclick <line>: click the editor gutter's breakpoint column at a (0-based) line
        a->add("gutterclick", [=](const QString& args){
            QTextCursor c(script->document()->findBlockByNumber(args.toInt()));
            const QPoint pos(4, script->cursorRect(c).center().y());
            QMouseEvent e(QEvent::MouseButtonPress, pos, script->mapToGlobal(pos),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            script->gutterPressed(&e);
        });
        // tabdump <file>: the text of the tab being edited; tabcursor <line> <column>: its cursor;
        // tabselect <index>: show a tab
        // fold <line>: the fold of the (0-based) line, as a click on its arrow in the gutter does (closed when open, open when closed)
        a->add("fold", [=](const QString& args){
            const int line = args.toInt();
            script->setFolded(line, !script->isFolded(line));
        });
        a->add("tabdump", [=](const QString& path){
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) f.write(editor->scriptWidget()->toPlainText().toUtf8());
        });
        a->add("tabcursor", [=](const QString& args){
            const auto p = args.split(' ');
            if (p.size() < 2) return;
            auto s = editor->scriptWidget();
            QTextCursor c(s->document()->findBlockByNumber(p[0].toInt()));
            c.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, p[1].toInt());
            s->setTextCursor(c);
            s->setFocus();
        });
        a->add("tabselect", [=](const QString& index){
            const int i = index.toInt();
            for (int k = 0; k < i; ++k) editor->nextTab();
            if (i == 0) editor->showScriptTab();
        });
        a->add("dump", [=](const QString& path){
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) f.write(editor->getScript().toUtf8());
        });
        // outdump <file>: the text of the output pane (what the script printed, its error)
        a->add("outdump", [=](const QString& path){
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                if (auto o = editor->findChild<QPlainTextEdit*>("Output")) f.write(o->toPlainText().toUtf8());
        });
        a->add("ctrlclick", [=](const QString& args){
            const auto p = args.split(' ');
            if (p.size() < 2) return;
            QTextCursor c(script->document()->findBlockByNumber(p[0].toInt()));
            c.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, p[1].toInt());
            const QPoint pos = script->cursorRect(c).center() + QPoint(2, 0);
            QMouseEvent press(QEvent::MouseButtonPress, pos, script->viewport()->mapToGlobal(pos),
                              Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
            QApplication::sendEvent(script->viewport(), &press);
            QMouseEvent release(QEvent::MouseButtonRelease, pos, script->viewport()->mapToGlobal(pos),
                                Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
            QApplication::sendEvent(script->viewport(), &release);
        });
        // tree <column> <text prefix>: click a model-tree row (column 0 =
        // name, 1 = eye, 2 = action); treedbl <text prefix>: double-click
        // (a row by the start of its name; `model@statement`: the row of the model under that statement, a shadow too)
        auto findRow = [=](const QString& prefix) -> QTreeWidgetItem* {
            auto scene = view->scenePanel();
            auto tree = scene->findChild<QTreeWidget*>();
            if (!tree) return nullptr;
            const int at = prefix.indexOf('@');
            if (at > 0) return scene->rowUnder(prefix.mid(at + 1).trimmed(), prefix.left(at).trimmed());
            for (auto row : tree->findItems(prefix, Qt::MatchStartsWith | Qt::MatchRecursive, 0))
            {
                if (!scene->isShadow(row)) return row;
            }
            return nullptr;
        };
        a->add("tree", [=](const QString& args){
            const int col = args.section(' ', 0, 0).toInt();
            auto row = findRow(args.section(' ', 1));
            if (!row) { std::cerr << "automation: no tree row " << args.toStdString() << "\n"; return; }
            auto tree = row->treeWidget();
            tree->setCurrentItem(row);
            emit tree->itemClicked(row, col);
        });
        a->add("import", [=](const QString& path){ importModel(path); });
        a->add("click", [=](const QString& name){
            if (auto b = findChild<QAbstractButton*>(name.trimmed())) b->click();
            else std::cerr << "automation: no button " << name.toStdString() << std::endl;
        });
        // slider <object name> <value>: set a slider (the result card's step: resultStep)
        a->add("slider", [=](const QString& args){
            const QString name = args.section(' ', 0, 0);
            if (auto s = findChild<QSlider*>(name)) s->setValue(args.section(' ', 1).toInt());
            else std::cerr << "automation: no slider " << name.toStdString() << std::endl;
        });
        a->add("sectionaxis", [=](const QString& axis){
            if (auto b = findChild<QAbstractButton*>("sectionAxis" + axis.trimmed().toUpper())) b->click();
        });
        // sectiondrag <fraction>: drag the plane's handle along its arrow
        a->add("sectiondrag", [=](const QString& v){
            QPointF knob, tip;
            if (!view->sectionHandlePoints(knob, tip))
            {
                std::cerr << "automation: no section handle" << std::endl;
                return;
            }
            const QPoint from = knob.toPoint();
            const QPoint to = (knob + (tip - knob) * v.toDouble()).toPoint();
            auto send = [&](QEvent::Type t, QPoint pos, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, pos, view->mapToGlobal(pos), b, bs, Qt::NoModifier);
                QApplication::sendEvent(view, &e);
            };
            send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            for (int k = 1; k <= 10; ++k)
            {
                send(QEvent::MouseMove, from + (to - from) * k / 10, Qt::NoButton, Qt::LeftButton);
            }
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
            std::cerr << "automation: dragged handle " << from.x() << "," << from.y()
                      << " -> " << to.x() << "," << to.y() << std::endl;
        });
        // combo <objectName> <index>, slider <objectName> <value>
        a->add("combo", [=](const QString& args){
            if (auto c = findChild<QComboBox*>(args.section(' ', 0, 0))) c->setCurrentIndex(args.section(' ', 1).toInt());
            else std::cerr << "automation: no combo " << args.toStdString() << std::endl;
        });
        a->add("slider", [=](const QString& args){
            if (auto s = findChild<QSlider*>(args.section(' ', 0, 0))) s->setValue(args.section(' ', 1).toInt());
            else std::cerr << "automation: no slider " << args.toStdString() << std::endl;
        });
        a->add("sectionpos", [=](const QString& v){
            if (auto s = findChild<QSlider*>("sectionOffset")) s->setValue(v.toInt());
        });
        // viewmouse <move|click|dbl|rclick|press|drag|release> <x> <y> [mods]: mouse event in the viewport
        // (press, drag and release are one drag in pieces, so that a picture can be taken in the middle of it);
        // viewmouse dragto <x> <y> <x2> <y2> [mods]: a whole drag.  mods: ctrl, shift, ctrl+shift, and middle (the
        // middle button instead of the left one: press, drag, release and dragto).
        // A click gives the viewport the keyboard first, as a real one does (events sent from here do not)
        a->add("viewmouse", [=](const QString& args){
            auto p = args.split(' ', Qt::SkipEmptyParts);
            Qt::KeyboardModifiers mods = Qt::NoModifier;
            Qt::MouseButton btn = Qt::LeftButton;
            if (!p.isEmpty() && !p.last().isEmpty() && !p.last()[0].isDigit() && p.last()[0] != '-' && p.size() > 3)
            {
                const QString m = p.takeLast().toLower();
                if (m.contains("ctrl")) mods |= Qt::ControlModifier;
                if (m.contains("shift")) mods |= Qt::ShiftModifier;
                if (m.contains("middle")) btn = Qt::MiddleButton;
            }
            if (p.size() < 3) return;
            const QPoint pos(p[1].toInt(), p[2].toInt());
            auto send = [&](QEvent::Type t, QPoint at, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, at, view->mapToGlobal(at), b, bs, mods);
                QApplication::sendEvent(view, &e);
            };
            auto takeFocus = [&]{ if (view->focusPolicy() & Qt::ClickFocus) view->setFocus(Qt::MouseFocusReason); };
            if (p[0] == "move")
            {
                send(QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton);
                return;
            }
            if (p[0] == "rclick")
            {
                // (a right-click that does not move opens a context menu: on a shape, or on empty space)
                send(QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton);
                takeFocus();
                send(QEvent::MouseButtonPress, pos, Qt::RightButton, Qt::RightButton);
                send(QEvent::MouseButtonRelease, pos, Qt::RightButton, Qt::NoButton);
                return;
            }
            if (p[0] == "press") { takeFocus(); send(QEvent::MouseButtonPress, pos, btn, btn); return; }
            if (p[0] == "drag") { send(QEvent::MouseMove, pos, Qt::NoButton, btn); return; }
            if (p[0] == "release") { send(QEvent::MouseButtonRelease, pos, btn, Qt::NoButton); return; }
            if (p[0] == "dragto" && p.size() >= 5)
            {
                const QPoint to(p[3].toInt(), p[4].toInt());
                takeFocus();
                send(QEvent::MouseButtonPress, pos, btn, btn);
                for (int k = 1; k <= 8; ++k)
                    send(QEvent::MouseMove, pos + (to - pos) * k / 8, Qt::NoButton, btn);
                send(QEvent::MouseButtonRelease, to, btn, Qt::NoButton);
                return;
            }
            takeFocus();
            send(p[0] == "dbl" ? QEvent::MouseButtonDblClick : QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
        });
        // treeclick <none|ctrl|shift> <text prefix>: a real click (mouse events, with the modifier held) on the name of a
        // model-tree row, so that Ctrl and Shift select several rows as they do for a user
        a->add("treeclick", [=](const QString& args){
            const QString m = args.section(' ', 0, 0).toLower();
            Qt::KeyboardModifiers mods = Qt::NoModifier;
            if (m.contains("ctrl")) mods |= Qt::ControlModifier;
            if (m.contains("shift")) mods |= Qt::ShiftModifier;
            auto row = findRow(args.section(' ', 1));
            if (!row) { std::cerr << "automation: no tree row " << args.toStdString() << "\n"; return; }
            auto tree = row->treeWidget();
            tree->scrollToItem(row);
            QApplication::processEvents();
            // (a scene that arrives there makes the rows again: the row is found again)
            row = findRow(args.section(' ', 1));
            if (!row) { std::cerr << "automation: no tree row " << args.toStdString() << "\n"; return; }
            const QPoint pos = QPoint(tree->columnViewportPosition(0) + tree->columnWidth(0) / 2, tree->visualItemRect(row).center().y());
            auto send = [&](QEvent::Type t, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, pos, tree->viewport()->mapToGlobal(pos), b, bs, mods);
                QApplication::sendEvent(tree->viewport(), &e);
            };
            send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        });
        // treebutton <column> <text prefix>: a real click (mouse events) on a button of a model-tree row: 1 the eye, 2 the gizmo, 3 the
        // lock, 4 the cache, 5 reimport, 6 reset, 7 delete -- the way a hand clicks it, which selects the row first
        a->add("treebutton", [=](const QString& args){
            const int col = args.section(' ', 0, 0).toInt();
            auto row = findRow(args.section(' ', 1));
            if (!row) { std::cerr << "automation: no tree row " << args.toStdString() << "\n"; return; }
            auto tree = row->treeWidget();
            tree->scrollToItem(row);
            QApplication::processEvents();
            // (a scene that arrives there makes the rows again: the row is found again)
            row = findRow(args.section(' ', 1));
            if (!row) { std::cerr << "automation: no tree row " << args.toStdString() << "\n"; return; }
            const QPoint pos(tree->columnViewportPosition(col) + tree->columnWidth(col) / 2, tree->visualItemRect(row).center().y());
            auto send = [&](QEvent::Type t, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, pos, tree->viewport()->mapToGlobal(pos), b, bs, Qt::NoModifier);
                QApplication::sendEvent(tree->viewport(), &e);
            };
            send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        });
        // treerclick <text prefix>: a right-click on the name of a model-tree row (an empty prefix: the empty space under the rows)
        a->add("treerclick", [=](const QString& args){
            auto tree = view->scenePanel()->findChild<QTreeWidget*>();
            if (!tree) return;
            QPoint pos(tree->viewport()->width() / 2, tree->viewport()->height() - 4);
            if (!args.trimmed().isEmpty())
            {
                auto row = findRow(args);
                if (!row) { std::cerr << "automation: no tree row " << args.toStdString() << "\n"; return; }
                tree->scrollToItem(row);
                QApplication::processEvents();
                pos = QPoint(tree->columnViewportPosition(0) + tree->columnWidth(0) / 2, tree->visualItemRect(row).center().y());
            }
            QContextMenuEvent e(QContextMenuEvent::Mouse, pos, tree->viewport()->mapToGlobal(pos));
            QApplication::sendEvent(tree->viewport(), &e);
        });
        // widgetgeom <objectName>: where a widget is in its parent, and how large it is (a card, for instance)
        a->add("widgetgeom", [=](const QString& name){
            auto w = findChild<QWidget*>(name.trimmed());
            if (!w) { std::cerr << "automation: no widget " << name.toStdString() << "\n"; return; }
            std::cerr << "automation: " << name.trimmed().toStdString() << " visible=" << w->isVisible() << " at " << w->x() << "," << w->y()
                      << " size " << w->width() << "x" << w->height() << "\n";
        });
        // widgetdrag <objectName> <x0> <y0> <x1> <y1>: the left button pressed at (x0, y0) of a widget, dragged to (x1, y1) and let go
        // (in the widget's own coordinates: a card is dragged by its header, resized from its edge)
        a->add("widgetdrag", [=](const QString& args){
            const QStringList p = args.split(' ', Qt::SkipEmptyParts);
            auto w = p.size() == 5 ? findChild<QWidget*>(p[0]) : nullptr;
            if (!w) { std::cerr << "automation: widgetdrag <objectName> x0 y0 x1 y1: no widget in " << args.toStdString() << "\n"; return; }
            const QPoint from(p[1].toInt(), p[2].toInt()), to(p[3].toInt(), p[4].toInt());
            // (the cursor is where the screen says: the widget moves under it, so its own coordinates are worked out each time)
            const QPoint start = w->mapToGlobal(from);
            auto send = [&](QEvent::Type t, const QPoint& global, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, w->mapFromGlobal(global), global, b, bs, Qt::NoModifier);
                QApplication::sendEvent(w, &e);
                QApplication::processEvents();
            };
            send(QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
            send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            for (int i = 1; i <= 8; ++i) send(QEvent::MouseMove, start + (to - from) * i / 8, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, start + (to - from), Qt::LeftButton, Qt::NoButton);
        });
        // editorrclick <line>: a right-click on a (0-based) line of the script in the text editor
        a->add("editorrclick", [=](const QString& args){
            Script* s = editor->scriptWidget();
            const QTextBlock b = s->document()->findBlockByNumber(args.trimmed().toInt());
            if (!b.isValid()) { std::cerr << "automation: no line " << args.toStdString() << "\n"; return; }
            const QPoint pos = s->cursorRect(QTextCursor(b)).center();
            QContextMenuEvent e(QContextMenuEvent::Mouse, pos, s->viewport()->mapToGlobal(pos));
            QApplication::sendEvent(s->viewport(), &e);
        });
        // selected: what is selected in the model tree (in the order it was selected) and lit up in the viewport
        a->add("selected", [=](const QString&){
            QStringList lines;
            for (int l : view->highlightedLines()) lines << QString::number(l);
            std::cerr << "automation: selected [" << view->scenePanel()->selectionKeys().join(", ").toStdString()
                      << "]  highlighted lines [" << lines.join(", ").toStdString() << "]\n";
        });
        // focuswho: which widget has the keyboard
        a->add("focuswho", [=](const QString&){
            QWidget* w = QApplication::focusWidget();
            std::cerr << "automation: focus is " << (w ? w->metaObject()->className() : "nothing") << " "
                      << (w ? w->objectName().toStdString() : std::string()) << "\n";
        });
        // popup open <path>: hover an entry of the open context menu, so that its submenu opens; popup pick <path>:
        // choose an entry; popup grab <file>: a picture of the window with the menus open.  A path is entry names
        // through the submenus, as "New 3D shape/sphere"
        a->add("popup", [=](const QString& args){
            const QString cmd = args.section(' ', 0, 0), rest = args.section(' ', 1).trimmed();
            QMenu* root = nullptr;
            for (QWidget* w : QApplication::topLevelWidgets())
            {
                auto m = qobject_cast<QMenu*>(w);
                if (m && m->isVisible() && !qobject_cast<QMenu*>(m->parentWidget())) root = m;
            }
            if (cmd == "grab")
            {
                QPixmap shot = this->grab();
                QPainter painter(&shot);
                for (QWidget* w : QApplication::topLevelWidgets())
                {
                    if (qobject_cast<QMenu*>(w) && w->isVisible())
                        painter.drawPixmap(this->mapFromGlobal(w->pos()), w->grab());
                }
                painter.end();
                shot.save(rest);
                return;
            }
            if (!root)
            {
                std::cerr << "automation: no context menu is open" << std::endl;
                return;
            }
            QMenu* menu = root;
            const QStringList path = rest.split('/');
            for (int k = 0; k < path.size(); ++k)
            {
                QAction* found = nullptr;
                for (QAction* act : menu->actions())
                    if (act->text().remove('&') == path[k]) found = act;
                if (!found)
                {
                    std::cerr << "automation: no menu entry " << path[k].toStdString() << " in " << rest.toStdString() << std::endl;
                    return;
                }
                if (k + 1 == path.size() && cmd == "pick")
                {
                    if (!found->isEnabled()) std::cerr << "automation: the entry " << rest.toStdString() << " is disabled" << std::endl;
                    else found->trigger();
                    root->close();
                    return;
                }
                menu->setActiveAction(found);
                if (!found->menu()) return;
                QMenu* parentMenu = menu;
                menu = found->menu();
                if (!menu->isVisible())
                    menu->popup(parentMenu->mapToGlobal(parentMenu->actionGeometry(found).topRight()));
            }
        });
        // viewzoom <x> <y> <steps>: turn the mouse wheel over a point of the viewport (positive: in)
        a->add("viewzoom", [=](const QString& args){
            const auto p = args.split(' ');
            if (p.size() < 3) return;
            const QPoint pos(p[0].toInt(), p[1].toInt());
            const int steps = p[2].toInt();
            QMouseEvent move(QEvent::MouseMove, pos, view->mapToGlobal(pos), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
            QApplication::sendEvent(view, &move);
            for (int i = 0; i < std::abs(steps); ++i)
            {
                QWheelEvent w(QPointF(pos), QPointF(view->mapToGlobal(pos)), QPoint(),
                              QPoint(0, steps > 0 ? 120 : -120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
                QApplication::sendEvent(view, &w);
            }
        });
        // viewdrag <x> <y> <dx> <dy>: press the viewport at (x, y), drag by (dx, dy) pixels in
        // steps, release (FielDes's own handles: dragging a surface)
        a->add("viewdrag", [=](const QString& args){
            const auto p = args.split(' ');
            if (p.size() < 4) return;
            const QPoint from(p[0].toInt(), p[1].toInt()), d(p[2].toInt(), p[3].toInt());
            auto send = [&](QEvent::Type t, QPoint pos, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, pos, view->mapToGlobal(pos), b, bs, Qt::NoModifier);
                QApplication::sendEvent(view, &e);
            };
            send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            for (int i = 1; i <= 10; ++i)
            {
                send(QEvent::MouseMove, from + d * i / 10, Qt::NoButton, Qt::LeftButton);
            }
            send(QEvent::MouseButtonRelease, from + d, Qt::LeftButton, Qt::NoButton);
        });
        // selectsurface <x> <y> [mode angle radius]: what the right-click menu does when confirmed
        a->add("selectsurface", [=](const QString& args){
            const auto p = args.split(' ', Qt::SkipEmptyParts);
            if (p.size() < 2) return;
            if (!view->selectSurfaceAt(QPoint(p[0].toInt(), p[1].toInt()), p.value(2, "flat"),
                                       p.value(3, "10").toDouble(), p.value(4, "0").toDouble()))
            {
                std::cerr << "automation: no shape at " << args.toStdString() << std::endl;
            }
        });
        // handledrag <kind 0|1|2> <axis 0-2> <dx> <dy>: press a part's gizmo (a move arrow, a
        // rotation ring or a scale square), drag by (dx, dy) pixels in steps, release
        a->add("handledrag", [=](const QString& args){
            const auto p = args.split(' ');
            if (p.size() < 4) return;
            QPoint from;
            if (!view->handleGripPoint(p[0].toInt(), p[1].toInt(), from))
            {
                std::cerr << "automation: no handle " << args.toStdString() << std::endl;
                return;
            }
            const QPoint d(p[2].toInt(), p[3].toInt());
            auto send = [&](QEvent::Type t, QPoint pos, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, pos, view->mapToGlobal(pos), b, bs, Qt::NoModifier);
                QApplication::sendEvent(view, &e);
            };
            send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            for (int i = 1; i <= 8; ++i)
            {
                send(QEvent::MouseMove, from + d * i / 8, Qt::NoButton, Qt::LeftButton);
            }
            send(QEvent::MouseButtonRelease, from + d, Qt::LeftButton, Qt::NoButton);
        });
        // varsteps <line>: the numbers of the var()s on a (0-based) line of the script are set in steps that each move another set
        // of them (Editor::testVarSteps) -- what a drag that moves different numbers of a line at each step writes
        a->add("varsteps", [=](const QString& args){
            std::cerr << "automation: varsteps found " << editor->testVarSteps(args.trimmed().toInt()) << " numbers" << std::endl;
        });
        // fielddrag <part> <dx> <dy> [steps]: the field viewer's disc is gripped by its arrow 1 or 2 (in its plane), the dot 3 or the
        // arrow along its normal 4, and dragged by (dx, dy) pixels in `steps` mouse moves (the way a hand drags it)
        a->add("fielddrag", [=](const QString& args){
            const auto p = args.split(' ', Qt::SkipEmptyParts);
            QPoint from;
            if (p.size() < 3 || !view->fieldGizmoPoint(p[0].toInt(), from))
            {
                std::cerr << "automation: no field gizmo part " << args.toStdString() << std::endl;
                return;
            }
            const QPoint d(p[1].toInt(), p[2].toInt());
            const int steps = std::max(1, p.value(3, "20").toInt());
            auto send = [&](QEvent::Type t, QPoint pos, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, pos, view->mapToGlobal(pos), b, bs, Qt::NoModifier);
                QApplication::sendEvent(view, &e);
            };
            send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            QElapsedTimer clock;
            clock.start();
            for (int i = 1; i <= steps; ++i)
            {
                send(QEvent::MouseMove, from + d * i / steps, Qt::NoButton, Qt::LeftButton);
                QApplication::processEvents();           // (a hand does not move faster than the program answers: events are served)
            }
            send(QEvent::MouseButtonRelease, from + d, Qt::LeftButton, Qt::NoButton);
            const QVector3D c = view->fieldViewCentre();
            std::cerr << "automation: fielddrag " << p[0].toStdString() << " took " << clock.elapsed() << " ms for " << steps
                      << " moves; the disc is at (" << c.x() << ", " << c.y() << ", " << c.z() << ")" << std::endl;
        });
        // menu <title>: open a menu of the menu bar (grabpopups then shows it); closemenus closes it
        a->add("menu", [=](const QString& title){
            for (auto act : menuBar()->actions())
            {
                QString t = act->text();
                t.remove('&');
                if (t == title.trimmed() && act->menu())
                {
                    act->menu()->popup(menuBar()->mapToGlobal(menuBar()->actionGeometry(act).bottomLeft()));
                    return;
                }
            }
            std::cerr << "automation: no menu " << title.toStdString() << std::endl;
        });
        a->add("closemenus", [=](const QString&){
            while (auto m = QApplication::activePopupWidget()) m->close();
        });
        a->add("viewsize", [=](const QString&){
            std::cerr << "automation: view " << view->width() << "x" << view->height() << "\n";
        });
        a->add("new", [=](const QString&){
            editor->setScript(QString());
            editor->setModified(false);
        });
        // treedump <file>: appends the model tree's top-level rows (their names) and the number of shapes the viewport
        // holds; open <file>: opens a script as the Open menu does
        // treescroll: the vertical scroll of the model tree, for a test that it stays where it was
        a->add("treescroll", [=](const QString&){
            std::cerr << "automation: treescroll " << view->scenePanel()->treeScroll() << "\n";
        });
        // treestate <file>: the models' rows with the lines they are on, whether they are shown / locked / cached and what their buttons
        // show (appended, with the time since the start in ms)
        a->add("treestate", [=](const QString& path){
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
            static QElapsedTimer clock;
            if (!clock.isValid()) clock.start();
            f.write(QString("== %1 ms\n%2").arg(clock.elapsed()).arg(view->scenePanel()->dumpState()).toUtf8());
        });
        // treeshift <delta>: the rows' lines that show and hide the models move by delta, the script stays (rows that are of another text)
        // (`treeshift <delta> blocks`: the lines of the lock / handles / cache statements instead)
        a->add("treeshift", [=](const QString& args){
            const QStringList p = args.split(' ', Qt::SkipEmptyParts);
            if (!p.isEmpty()) view->scenePanel()->debugShiftLines(p[0].toInt(), p.size() > 1 && p[1] == "blocks");
        });
        a->add("treedump", [=](const QString& path){
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
            QString text = QString("-- viewport shapes: %1\n").arg(view->shapeCount());
            text += view->scenePanel()->dumpRows();
            f.write(text.toUtf8());
        });
        // treedrop <source prefix> > <target prefix> > <above|on|below|end> [ctrl] [hold]: drag a row of the model tree and drop it
        // (`hold`: the button stays down until `treerelease`)
        // (the mouse events of a hand -- press, moves, release -- sent to the tree, so the code that runs is the one the real
        // mouse runs); `ctrl`: with Ctrl held, which makes the drop a copy (a reference).  A row may be named `model@statement`:
        // the row of that model under that statement, a shadow too
        // (a drag held with `hold`: where the mouse is, for treerelease)
        auto heldDrag = std::make_shared<QPair<QPoint, Qt::KeyboardModifiers>>();
        a->add("treerelease", [=](const QString&){
            QWidget* vp = view->scenePanel()->treeViewport();
            QMouseEvent e(QEvent::MouseButtonRelease, heldDrag->first, vp->mapToGlobal(heldDrag->first), Qt::LeftButton,
                          Qt::NoButton, heldDrag->second);
            QApplication::sendEvent(vp, &e);
            QApplication::processEvents();
        });
        a->add("treedrop", [=](const QString& args){
            const QStringList p = args.split('>');
            if (p.size() < 3) { std::cerr << "automation: treedrop <source> > <target> > <above|on|below|end> [ctrl]\n"; return; }
            auto src = findRow(p[0].trimmed());
            const QStringList words = p[2].trimmed().split(' ', Qt::SkipEmptyParts);
            const QString where = words.value(0);
            Qt::KeyboardModifiers mods = Qt::NoModifier;
            bool hold = false;
            for (int k = 1; k < words.size(); ++k)
            {
                if (words[k] == "ctrl") mods |= Qt::ControlModifier;
                else if (words[k] == "hold") hold = true;
                else { std::cerr << "automation: treedrop: unknown word " << words[k].toStdString() << "\n"; return; }
            }
            auto dst = where == "end" ? nullptr : findRow(p[1].trimmed());
            if (!src || (!dst && where != "end")) { std::cerr << "automation: no tree row in " << args.toStdString() << "\n"; return; }
            auto tree = src->treeWidget();
            auto scene = view->scenePanel();
            QWidget* vp = scene->treeViewport();
            auto send = [&](QEvent::Type t, const QPoint& pos, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, pos, vp->mapToGlobal(pos), b, bs, mods);
                QApplication::sendEvent(vp, &e);
                QApplication::processEvents();
            };
            tree->scrollToItem(src);
            QApplication::processEvents();
            const QPoint from(60, tree->visualItemRect(src).center().y());
            QPoint to(8, vp->height() - 2);
            if (dst)
            {
                tree->scrollToItem(dst);
                QApplication::processEvents();
                const QRect r = tree->visualItemRect(dst);
                to = QPoint(60, where == "above" ? r.top() + r.height() / 8 : where == "below" ? r.bottom() - r.height() / 8
                                                                                              : r.center().y());
            }
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            for (int i = 1; i <= 8; ++i)
                send(QEvent::MouseMove, from + (to - from) * i / 8, Qt::NoButton, Qt::LeftButton);
            std::cerr << "automation: dragging " << (scene->dragging() ? "yes" : "no") << " at " << where.toStdString() << "\n";
            if (hold)
            {
                // (the button stays down: a grab shows the drag as it is; treerelease lets go)
                *heldDrag = qMakePair(to, mods);
                return;
            }
            if (qEnvironmentVariableIsSet("FIELDES_SHOW_DIALOGS"))
            {
                // (a question may be asked by the drop: the mouse is let go from the event loop, so that the test goes on)
                QTimer::singleShot(50, vp, [=] {
                    QMouseEvent e(QEvent::MouseButtonRelease, to, vp->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, mods);
                    QApplication::sendEvent(vp, &e);
                });
                return;
            }
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        });
        // blocksfolder <path>: Settings > Blocks folder, chosen (empty: the default one)
        a->add("blocksfolder", [=](const QString& path){ setBlocksFolder(path.trimmed()); });
        a->add("open", [=](const QString& path){
            if (loadFile(path)) setFilename(path);
        });
        // setting <set_bounds|set_resolution|set_quality> <index> <text> [esc]: type into one field of the render settings in the
        // tree and press Enter (or Esc), as a user does
        a->add("setting", [=](const QString& args){
            const QStringList p = args.split(' ', Qt::SkipEmptyParts);
            if (p.size() < 3) { std::cerr << "automation: setting <fn> <index> <text> [esc]\n"; return; }
            auto scene = view->scenePanel();
            scene->showSettings(true);
            QApplication::processEvents();
            QLineEdit* f = scene->settingEditor(p[0], p[1].toInt());
            if (!f) { std::cerr << "automation: no field " << args.toStdString() << "\n"; return; }
            f->setFocus();
            f->selectAll();
            f->setText(p[2]);
            const int key = p.size() > 3 && p[3] == "esc" ? Qt::Key_Escape : Qt::Key_Return;
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier), release(QEvent::KeyRelease, key, Qt::NoModifier);
            QApplication::sendEvent(f, &press);
            QApplication::sendEvent(f, &release);
            QApplication::processEvents();
            std::cerr << "automation: setting " << p[0].toStdString() << " " << p[1].toStdString() << " shows '" << f->text().toStdString() << "'\n";
        });
        // (the editor of a name in the tree: the field that has the keyboard, else the one that is open in the tree -- a window that
        // is not the active one has no keyboard focus, and a test must not depend on that)
        auto nameEditor = [=]() -> QLineEdit* {
            auto f = qobject_cast<QLineEdit*>(QApplication::focusWidget());
            if (f && !f->property("fn").isValid()) return f;
            for (auto e : view->scenePanel()->treeViewport()->findChildren<QLineEdit*>())
                if (e->isVisible() && !e->property("fn").isValid()) return e;
            return nullptr;
        };
        // renamekey <enter|esc>: the key on the name editor that is open (after treerename ... typeonly)
        a->add("renamekey", [=](const QString& what){
            auto f = nameEditor();
            if (!f) { std::cerr << "automation: no name editor is open\n"; return; }
            const int key = what.trimmed() == "esc" ? Qt::Key_Escape : Qt::Key_Return;
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier), release(QEvent::KeyRelease, key, Qt::NoModifier);
            QApplication::sendEvent(f, &press);
            QApplication::sendEvent(f, &release);
            QApplication::processEvents();
        });
        // treerename <row prefix> > <new name> [esc|f2]: double-click the name of a row (or select it and press F2), type the new name, Enter
        // (or Esc)
        a->add("treerename", [=](const QString& args){
            const QStringList p = args.split('>');
            if (p.size() < 2) { std::cerr << "automation: treerename <row> > <new name> [esc|f2]\n"; return; }
            auto row = findRow(p[0].trimmed());
            if (!row) { std::cerr << "automation: no tree row " << args.toStdString() << "\n"; return; }
            const QString newName = p[1].trimmed().section(' ', 0, 0);
            const QString how = p[1].trimmed().section(' ', 1, 1);
            auto tree = row->treeWidget();
            QWidget* vp = view->scenePanel()->treeViewport();
            tree->scrollToItem(row);
            QApplication::processEvents();
            const QPoint at(70, tree->visualItemRect(row).center().y());
            auto send = [&](QEvent::Type t, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent e(t, at, vp->mapToGlobal(at), b, bs, Qt::NoModifier);
                QApplication::sendEvent(vp, &e);
                QApplication::processEvents();
            };
            if (how == "f2")
            {
                tree->setCurrentItem(row);
                tree->setFocus();
                QKeyEvent press(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier), release(QEvent::KeyRelease, Qt::Key_F2, Qt::NoModifier);
                QApplication::sendEvent(tree, &press);
                QApplication::sendEvent(tree, &release);
            }
            else
            {
                send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
                send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
                send(QEvent::MouseButtonDblClick, Qt::LeftButton, Qt::LeftButton);
                send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
            }
            QApplication::processEvents();
            auto field = nameEditor();
            if (!field) { std::cerr << "automation: no name editor opened\n"; return; }
            std::cerr << "automation: name editor opened on '" << field->text().toStdString() << "'\n";
            // (type | typeonly: the name is typed a key at a time, as a hand does -- the script follows every key, and the editor is
            // built again under the hand; typeonly leaves it open, for a dump of the script before Enter or Esc)
            if (how == "type" || how == "typeonly")
            {
                field->selectAll();
                for (const QChar c : newName)
                {
                    auto f = nameEditor();
                    if (!f) { std::cerr << "automation: the name editor was lost\n"; return; }
                    QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
                    QApplication::sendEvent(f, &key);
                    QApplication::processEvents();
                }
                if (how == "typeonly") return;
                if (auto f = nameEditor()) field = f;
            }
            else field->setText(newName);
            const int key = how == "esc" ? Qt::Key_Escape : Qt::Key_Return;
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier), release(QEvent::KeyRelease, key, Qt::NoModifier);
            QApplication::sendEvent(field, &press);
            QApplication::sendEvent(field, &release);
            QApplication::processEvents();
        });
        // settingtype <set_bounds|set_resolution|set_quality> <index> <text> [enter]: type into one field of the render settings a key
        // at a time (the script follows every key); Enter after it only when asked
        a->add("settingtype", [=](const QString& args){
            const QStringList p = args.split(' ', Qt::SkipEmptyParts);
            if (p.size() < 3) { std::cerr << "automation: settingtype <fn> <index> <text> [enter]\n"; return; }
            auto scene = view->scenePanel();
            scene->showSettings(true);
            QApplication::processEvents();
            QLineEdit* f = scene->settingEditor(p[0], p[1].toInt());
            if (!f) { std::cerr << "automation: no field " << args.toStdString() << "\n"; return; }
            f->setFocus();
            f->selectAll();
            for (const QChar c : p[2])
            {
                // (the tree is built again as the script runs: the field is looked up again each key)
                f = scene->settingEditor(p[0], p[1].toInt());
                if (!f) break;
                QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
                QApplication::sendEvent(f, &key);
                QApplication::processEvents();
            }
            f = scene->settingEditor(p[0], p[1].toInt());
            if (f && p.size() > 3 && p[3] == "enter")
            {
                QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier), release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
                QApplication::sendEvent(f, &press);
                QApplication::sendEvent(f, &release);
                QApplication::processEvents();
            }
            std::cerr << "automation: settingtype " << p[0].toStdString() << " " << p[1].toStdString() << " shows '"
                      << (f ? f->text().toStdString() : std::string("?")) << "'\n";
        });
        // rowpos <row>: where the row is, in the coordinates of a grab of the window (for the tests that use the real mouse)
        a->add("rowpos", [=](const QString& args){
            auto row = findRow(args.trimmed());
            if (!row) { std::cerr << "automation: no tree row " << args.toStdString() << "\n"; return; }
            auto scene = view->scenePanel();
            QWidget* vp = scene->treeViewport();
            auto tree = row->treeWidget();
            tree->scrollToItem(row);
            QApplication::processEvents();
            const QRect r = tree->visualItemRect(row);
            const QPoint p = vp->mapTo(this, QPoint(70, r.center().y()));
            std::cerr << "automation: rowpos " << args.trimmed().toStdString() << " " << p.x() << " " << p.y() << "\n";
        });
        a->add("treedbl", [=](const QString& args){
            auto row = findRow(args);
            if (!row) { std::cerr << "automation: no tree row " << args.toStdString() << "\n"; return; }
            emit row->treeWidget()->itemDoubleClicked(row, 0);
        });
        a->add("grabwidget", [=](const QString& args){
            // grabwidget <objectName> <file>
            const auto name = args.section(' ', 0, 0);
            const auto path = args.section(' ', 1);
            for (auto w : QApplication::topLevelWidgets())
            {
                if (w->objectName() == name && w->isVisible())
                {
                    w->grab().save(path);
                    return;
                }
                if (auto c = w->findChild<QWidget*>(name))
                {
                    c->grab().save(path);
                    return;
                }
            }
        });
        a->add("grabpopups", [=](const QString& path){
            // Composite every visible top-level window (popups, dialogs)
            // over the main window, at their screen positions
            QPixmap base = grab();
            QPainter painter(&base);
            for (auto w : QApplication::topLevelWidgets())
            {
                if (w != this && w->isVisible())
                {
                    painter.drawPixmap(mapFromGlobal(w->mapToGlobal(QPoint(0, 0))), w->grab());
                }
            }
            painter.end();
            base.save(path);
        });
        // grabmenus <file>: the open context menu and the submenus that are open, as one picture
        a->add("grabmenus", [=](const QString& path){
            QRect all;
            QList<QMenu*> menus;
            for (QWidget* w : QApplication::topLevelWidgets())
            {
                auto m = qobject_cast<QMenu*>(w);
                if (m && m->isVisible())
                {
                    menus << m;
                    all = all.united(QRect(m->pos(), m->size()));
                }
            }
            if (menus.isEmpty()) return;
            QPixmap shot(all.size());
            shot.fill(Qt::transparent);
            QPainter painter(&shot);
            for (auto m : menus) painter.drawPixmap(m->pos() - all.topLeft(), m->grab());
            painter.end();
            shot.save(path);
        });
        // grabcrop <x> <y> <width> <height> <file>: a part of the window
        a->add("grabcrop", [=](const QString& args){
            const auto p = args.split(' ', Qt::SkipEmptyParts);
            if (p.size() < 5) return;
            grab(QRect(p[0].toInt(), p[1].toInt(), p[2].toInt(), p[3].toInt())).save(p.mid(4).join(' '));
        });
        if (a->load(automation_file))
        {
            QTimer::singleShot(1500, a, &Automation::start);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

bool Window::openFile(const QString& name)
{
    CHECK_UNSAVED();

    if (loadFile(name))
    {
        setFilename(name);
        return true;
    }
    emit(setAutoload(false));
    return false;
}

bool Window::onOpen(bool)
{
    CHECK_UNSAVED();

    QString f = QFileDialog::getOpenFileName(this, "Open",
            workingDirectory(), "FielDes scripts (*.py);;Any files (*)");
    if (!f.isEmpty())
    {
        return openFile(f);
    }
    return false;
}

bool Window::onOpenExample(bool)
{
    CHECK_UNSAVED();

    const QString dir = QDir(QCoreApplication::applicationDirPath()).filePath("examples");
    if (!QDir(dir).exists())
    {
        QMessageBox m(this);
        m.setText("The examples folder was not found");
        m.setInformativeText("The examples are the files of the folder <code>examples</code> next to the program, and there is none at<br><code>" +
                             QDir::toNativeSeparators(dir) + "</code>");
        m.addButton(QMessageBox::Ok);
        m.setIcon(QMessageBox::Information);
        m.exec();
        return false;
    }
    const QString f = QFileDialog::getOpenFileName(this, "Open an example", dir, "FielDes examples (*.py);;Any files (*)");
    return f.isEmpty() ? false : openFile(f);
}

bool Window::onOpenViewer(bool)
{
    auto result = onOpen();
    if (result)
    {
        emit(setAutoload(true));
    }
    return result;
}

bool Window::onRevert(bool)
{
    CHECK_UNSAVED();
    Q_ASSERT(!filename.isEmpty());
    return loadFile(filename);
}

bool Window::loadFile(QString f, bool reload)
{
    QFile file(f);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox m(this);
        m.setText("Failed to open file");
        m.setInformativeText("<code>" + f + "</code><br>does not exist");
        m.addButton(QMessageBox::Ok);
        m.setIcon(QMessageBox::Critical);
        m.setWindowModality(Qt::WindowModal);
        m.exec();
        return false;
    }
    else
    {
        editor->setScript(file.readAll(), reload);
        editor->setModified(false);
        editor->guessLanguage(QFileInfo(file.fileName()).suffix().toLower());
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////////

void Window::onAutoLoad(const QString&)
{
    if (QFile(filename).open(QIODevice::ReadOnly))
    {
        if (autoreload)
        {
            Q_ASSERT(!filename.isEmpty());
            loadFile(filename, true);
        }

        // Some editors don't edit but replace the file so the watcher thinks
        // the file was deleted and the signal is only sent once.
        // Re-watching the file is mandatory for those cases.
        if (QFile::exists(filename)) {
            watcher.addPath(filename);
        }
    }
    else // File was deleted. Waiting for new file
    {
        watcher.addPath(QFileInfo(filename).path());
    }
}

////////////////////////////////////////////////////////////////////////////////

void Window::onAutoLoadPath(const QString&)
{
    // file was created
    if (QFile(filename).open(QIODevice::ReadOnly))
    {
        watcher.removePaths(watcher.directories());
        watcher.addPath(filename);
        if (autoreload)
        {
            Q_ASSERT(!filename.isEmpty());
            loadFile(filename, true);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

bool Window::saveFile(QString f)
{
    QFile file(f);
    if (!QFileInfo(QFileInfo(f).path()).isWritable())
    {

        QMessageBox m(this);
        m.setText("Failed to save file");
        m.setInformativeText("<code>" + f + "</code><br>is not writable");
        m.addButton(QMessageBox::Ok);
        m.setIcon(QMessageBox::Critical);
        m.setWindowModality(Qt::WindowModal);
        m.exec();
        return false;
    }
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox m(this);
        m.setText("Failed to save file");
        m.setInformativeText("<code>" + f + "</code><br>does not exist");
        m.addButton(QMessageBox::Ok);
        m.setIcon(QMessageBox::Critical);
        m.setWindowModality(Qt::WindowModal);
        m.exec();
        return false;
    }
    else
    {
        QTextStream out(&file);
        out << editor->getScript();

        editor->setModified(false);
        return true;
    }
}

bool Window::onSave(bool)
{
    // (a file opened from the script in its own tab is saved by its own Save)
    if (editor->saveCurrentAuxiliary())
    {
        return true;
    }
    if (filename.isEmpty() || filename.startsWith(":/"))
    {
        return onSaveAs();
    }
    else
    {
        return saveFile(filename);
    }
}

void Window::onCancelRender(bool)
{
    view->cancelShapes();
}

void Window::onAutosave()
{
    if (automated || filename.isEmpty() || filename.startsWith(":/") || !isWindowModified())
    {
        return;
    }

    // Deliberately bypasses saveFile(), which pops a blocking error dialog
    // on failure -- that's fine for an explicit Save, but wrong for a
    // silent background timer (e.g. a transient OneDrive lock would pop a
    // dialog every 5 seconds, forever). Fail silently instead; the next
    // tick (or an explicit Save) will catch up.
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly))
    {
        QTextStream out(&file);
        out << editor->getScript();
        editor->setModified(false);
    }
}

bool Window::onSaveAs(bool)
{
    QString f = QFileDialog::getSaveFileName(this, "Save as",
            workingDirectory(),
            QString("FielDes scripts (*%1);;Any files (*)").arg(editor->getExtension()));
    if (!f.isEmpty())
    {
        if (saveFile(f))
        {
            setFilename(f);
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////

bool Window::onNew(bool)
{
    return reset(Language::LANGUAGE_NONE);
}

void Window::closeEvent(QCloseEvent* event)
{
    if (closing)
    {
        event->accept();
    }
    else
    {
        switch (checkUnsaved())
        {
            case QMessageBox::Save:     onSave();   /* FALLTHROUGH */
            case QMessageBox::Ok:                   /* FALLTHROUGH */
            case QMessageBox::Discard:  event->accept(); break;
            case QMessageBox::Cancel:   event->ignore(); break;
            default:                    assert(false);
        }
        closing = event->isAccepted();
        if (closing)
        {
            if (!automated)
            {
                settings.setValue("window-geometry", saveGeometry());
                settings.setValue("window-state", saveState());
                if (auto sp = qobject_cast<QSplitter*>(centralWidget()))
                {
                    settings.setValue("splitter-state", sp->saveState());
                }
            }
            view->cancelShapes();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void Window::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() &&
        event->mimeData()->urls().size())
    {
        const auto f = event->mimeData()->urls().front().fileName().toLower();
        if (f.endsWith(".py") ||
            f.endsWith(".step") || f.endsWith(".stp") || f.endsWith(".stl") ||
            f.endsWith(".obj") || f.endsWith(".ply") || f.endsWith(".3mf") ||
            f.endsWith(".glb") || f.endsWith(".gltf"))
        {
            event->acceptProposedAction();
        }
    }
}

bool Window::dropEvent_(QDropEvent* event)
{
    const QString local = event->mimeData()->urls().first().toLocalFile();
    const QString ext = QFileInfo(local).suffix().toLower();
    if (ext == "step" || ext == "stp" || ext == "stl" || ext == "obj" ||
        ext == "ply" || ext == "3mf" || ext == "glb" || ext == "gltf")
    {
        importModel(local);   // models are added to the current script
        return true;
    }

    CHECK_UNSAVED();

    QString f = local.isEmpty() ? event->mimeData()->urls().first().path() : local;
    if (!f.isEmpty() && loadFile(f))
    {
        setFilename(f);
        return true;
    }
    return false;
}

void Window::dropEvent(QDropEvent* event)
{
    dropEvent_(event);
}

////////////////////////////////////////////////////////////////////////////////

QMessageBox::StandardButton Window::checkUnsaved()
{
    const QStringList others = editor->modifiedAuxiliary();
    if (!others.isEmpty() && !automated)
    {
        QMessageBox m(this);
        m.setText("Do you want to save your changes to " + others.join(", ") + "?");
        m.setInformativeText("If you don't save, your changes will be lost");
        m.addButton(QMessageBox::Discard);
        m.addButton(QMessageBox::Cancel);
        m.addButton(QMessageBox::Save);
        m.setIcon(QMessageBox::Warning);
        m.setWindowModality(Qt::WindowModal);
        const int r = m.exec();
        if (r == QMessageBox::Cancel) return QMessageBox::Cancel;
        if (r == QMessageBox::Save && !editor->saveAllAuxiliary()) return QMessageBox::Cancel;
    }
    if (isWindowModified())
    {
        QMessageBox m(this);
        m.setText("Do you want to save your changes to this document?");
        m.setInformativeText("If you don't save, your changes will be lost");
        m.addButton(QMessageBox::Discard);
        m.addButton(QMessageBox::Cancel);
        m.addButton(QMessageBox::Save);
        m.setIcon(QMessageBox::Warning);
        m.setWindowModality(Qt::WindowModal);
        return static_cast<QMessageBox::StandardButton>(m.exec());
    }
    else
    {
        return QMessageBox::Ok;
    }
}

void Window::addRecentFile(const QString& f)
{
    if (f.isEmpty() || f.startsWith(":/") || automated)
    {
        return;
    }
    QStringList r = settings.value("recent-files").toStringList();
    const QString abs = QFileInfo(f).absoluteFilePath();
    r.removeAll(abs);
    r.prepend(abs);
    while (r.size() > 12)
    {
        r.removeLast();
    }
    settings.setValue("recent-files", r);
    updateRecentMenu();
}

void Window::addRecentImport(const QString& f)
{
    if (f.isEmpty() || automated) return;
    QStringList r = settings.value("recent-imports").toStringList();
    const QString abs = QFileInfo(f).absoluteFilePath();
    r.removeAll(abs);
    r.prepend(abs);
    while (r.size() > 12) r.removeLast();
    settings.setValue("recent-imports", r);
}

void Window::showRecent(QWidget* below, bool imports)
{
    // The list of the files opened (scripts) or imported (models) lately, newest first; one that is gone is left out
    auto menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QString key = imports ? "recent-imports" : "recent-files";
    int n = 0;
    for (const auto& f : settings.value(key).toStringList())
    {
        const QFileInfo fi(f);
        if (!fi.exists()) continue;
        ++n;
        auto a = menu->addAction(fi.fileName() + "    " + QDir::toNativeSeparators(fi.path()));
        a->setToolTip(QDir::toNativeSeparators(fi.absoluteFilePath()));
        connect(a, &QAction::triggered, this, [=]{ if (imports) importModel(f); else openFile(f); });
    }
    if (n == 0)
    {
        menu->addAction(imports ? "No model has been imported yet" : "No script has been opened yet")->setEnabled(false);
    }
    else
    {
        menu->addSeparator();
        connect(menu->addAction("Clear list"), &QAction::triggered, this, [=]{
            settings.remove(key);
            updateRecentMenu();
        });
    }
    menu->popup(below->mapToGlobal(QPoint(-30, below->height())));
}

void Window::updateRecentMenu()
{
    if (!recent_menu)
    {
        return;
    }
    recent_menu->clear();
    int n = 0;
    for (const auto& f : settings.value("recent-files").toStringList())
    {
        const QFileInfo fi(f);
        if (!fi.exists())
        {
            continue;
        }
        ++n;
        const QString text = (n <= 9 ? QString("&%1  ").arg(n) : QString("    ")) +
            fi.fileName() + "    " + QDir::toNativeSeparators(fi.path());
        auto a = recent_menu->addAction(text);
        connect(a, &QAction::triggered, this, [=]{ openFile(f); });
    }
    if (n)
    {
        recent_menu->addSeparator();
        connect(recent_menu->addAction("Clear list"), &QAction::triggered, this, [this]{
            settings.remove("recent-files");
            updateRecentMenu();
        });
    }
    recent_menu->setEnabled(n > 0);
}

void Window::setFilename(const QString& f)
{
    filename = f;
    editor->setFilePath(f);
    addRecentFile(f);
    if (filename.startsWith(":/"))
    {
        QString title = QFileInfo(filename).fileName() + " (read-only)";
        #if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
            setWindowTitle(title+"[*]");
        #else
            setWindowTitle(title);
        #endif
    }
    else
    {
        #if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
            setWindowTitle(QFileInfo(filename).fileName() + "[*]");
        #else
            setWindowTitle(QString());
        #endif
        setWindowFilePath(f);
        // Scripts run in their own folder, so the relative paths they use ("step/part.step") work
        if (!filename.isEmpty()) QDir::setCurrent(QFileInfo(filename).absolutePath());
    }

    // Store the target file as our autoreload target
    // (even though we'll only do reloading if the menu option is set)
    auto watched_files = watcher.files();
    if (!watched_files.empty())
    {
        watcher.removePaths(watched_files);
    }
    if (!filename.startsWith(":/") && !filename.isEmpty())
    {
        watcher.addPath(filename);
    }
}

QString Window::workingDirectory() const
{
    return (filename.startsWith(":/") || filename.isEmpty())
        ? QDir::homePath()
        : QFileInfo(filename).dir().absolutePath();
}

////////////////////////////////////////////////////////////////////////////////

void Window::onExportReady(QList<const libfive::Mesh*> shapes)
{
    disconnect(view, &View::meshesReady, this, &Window::onExportReady);
    if (!libfive::Mesh::saveSTL(export_filename.toStdString(),
                                std::list<const libfive::Mesh*>(shapes.begin(), shapes.end())))
    {
        QMessageBox m(this);
        m.setText("Could not save file");
        m.setInformativeText("Check the console for more information");
        m.addButton(QMessageBox::Ok);
        m.setIcon(QMessageBox::Critical);
        m.setWindowModality(Qt::WindowModal);
        m.exec();
    }
    emit(exportDone());
}

void Window::onExport(bool)
{
    export_filename = QFileDialog::getSaveFileName(
            this, "Export STL", workingDirectory(), "STL files (*.stl);;Any files (*)");
    if (export_filename.isEmpty())
    {
        return;
    }
    if (!export_filename.endsWith(".stl", Qt::CaseInsensitive))
    {
        export_filename += ".stl";
    }

    connect(view, &View::meshesReady, this, &Window::onExportReady);

    auto p = new QProgressDialog(this);
    p->setCancelButton(nullptr);
    p->setWindowModality(Qt::WindowModal);
    p->setLabelText("Exporting...");
    p->setMaximum(0);

    // If we cancel the export (by pressing escape), then we shouldn't
    // run the final export step (of actually saving the meshes)
    connect(p, &QProgressDialog::rejected, this, [=](){
            disconnect(view, &View::meshesReady, this, &Window::onExportReady);
            this->export_filename = ""; });

    // Delete the progress dialog when we finish or cancel the export
    connect(this, &Window::exportDone, p, &QProgressDialog::deleteLater);
    connect(p, &QProgressDialog::rejected, p, &QProgressDialog::deleteLater);

    p->show();
    view->checkMeshes();
}

////////////////////////////////////////////////////////////////////////////////

void Window::onAbout(bool)
{
    QString info = "<b>FielDes</b> &mdash; field-driven design<br><br>"
                   "Version 0.1.0 (beta)<br>"
                   "Built on the kernel of <a href=\"https://github.com/libfive/libfive\">libfive</a> "
                   "by Matt Keeter<br><br>";
    const QString revision = libfive_git_revision();
    if (revision != "N/A") info += "Kernel revision: <code>" + revision + "</code><br>";
    info += "Licence: GPL 2 or later (application), MPL 2.0 (kernel and Python library)";
    QMessageBox::about(this, "About FielDes", info);
}

void Window::onImportModel(bool)
{
    const QString f = QFileDialog::getOpenFileName(
            this, "Import model", workingDirectory(),
            "CAD models (*.step *.stp *.STEP *.STP *.stl *.STL *.obj *.OBJ *.ply *.PLY *.3mf *.3MF *.glb *.GLB *.gltf *.GLTF);;"
            "STEP (*.step *.stp);;Triangle meshes (*.stl *.obj *.ply *.3mf *.glb *.gltf);;Any files (*)");
    if (!f.isEmpty())
    {
        importModel(f);
    }
}

void Window::importModel(const QString& path)
{
    addRecentImport(path);
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    const bool mesh = (ext == "stl" || ext == "obj" || ext == "ply" || ext == "3mf" ||
                      ext == "glb" || ext == "gltf");
    const QString script = editor->getScript();
    const QStringList lines = script.split('\n');

    // A fresh, valid variable name from the file name
    QString base = fi.completeBaseName().toLower();
    base.replace(QRegularExpression("[^a-z0-9_]+"), "_");
    base.remove(QRegularExpression("^_+|_+$"));
    if (base.isEmpty()) base = "model";
    if (base[0].isDigit()) base = "m_" + base;
    QString name = base;
    for (int n = 2; QRegularExpression("\\b" + name + "\\b").match(script).hasMatch(); ++n)
    {
        name = base + "_" + QString::number(n);
    }

    const QString nativePath = QDir::toNativeSeparators(fi.absoluteFilePath());
    QStringList code;
    code << "" << "# Imported model: " + fi.fileName();
    QString roiExpr;
    if (mesh)
    {
        // STL / OBJ / PLY files carry no units: say so explicitly, so it's
        // obvious where to change it (3MF files state theirs)
        if (ext == "3mf" || ext == "glb" || ext == "gltf")
            code << QString("%1 = import_model(r\"%2\")").arg(name, nativePath);
        else
            code << QString("%1 = import_model(r\"%2\", file_units=\"mm\")").arg(name, nativePath);
    }
    else
    {
        code << QString("%1 = import_model(r\"%2\")").arg(name, nativePath);
    }
    roiExpr = name;
    m_pendingImport = name;   // part display lines follow once evaluated

    // Region of interest over every import in the script plus this one
    const auto sceneDoc = QJsonDocument::fromJson(m_lastScene.toUtf8()).object();
    QStringList rois;
    for (const auto v : sceneDoc["items"].toArray())
    {
        const auto it = v.toObject();
        if (it["kind"].toString() == "import" && it.contains("roi_expr"))
            rois << it["roi_expr"].toString();
    }
    rois << roiExpr;
    const QString roiArgs = rois.join(", ");

    // The existing render-region statements move below the new import
    // (they must come after every model they refer to): drop them, found
    // by their exact statement spans when known, else by a single-line match
    QVector<bool> drop(lines.size(), false);
    const auto sceneSettings = sceneDoc["settings"].toObject();
    for (const QString fn : {QString("set_bounds"), QString("set_resolution")})
    {
        const auto span = sceneSettings[fn].toObject()["span"].toArray();
        if (span.size() == 4)
        {
            for (int i = span[0].toInt() - 1; i <= span[2].toInt() - 1 && i < lines.size(); ++i)
                drop[i] = true;
        }
        else
        {
            const QRegularExpression re("^\\s*view\\." + fn + "\\(.*\\)\\s*$");
            for (int i=0; i < lines.size(); ++i)
                if (re.match(lines[i]).hasMatch()) drop[i] = true;
        }
    }
    code << "view.set_bounds(*roi(" + roiArgs + "))";
    code << "view.set_resolution(roi_resolution(" + roiArgs + "))";

    // The quality statement moves along with them (keeping its value), so
    // the render settings stay together; without one the default-quality
    // warning would show
    QString quality = "view.set_quality(8)";
    const auto qspan = sceneSettings["set_quality"].toObject()["span"].toArray();
    if (qspan.size() == 4)
    {
        QStringList text;
        for (int i = qspan[0].toInt() - 1; i <= qspan[2].toInt() - 1 && i < lines.size(); ++i)
        {
            text << lines[i];
            drop[i] = true;
        }
        quality = text.join('\n').trimmed();
    }
    else
    {
        const QRegularExpression re(R"(^\s*view\.set_quality\(.*\)\s*$)");
        for (int i=0; i < lines.size(); ++i)
        {
            if (re.match(lines[i]).hasMatch())
            {
                quality = lines[i].trimmed();
                drop[i] = true;
            }
        }
    }
    if (qspan.size() == 4 || !QRegularExpression(R"(\bview\.quality\b)").match(script).hasMatch())
    {
        code << quality;
    }

    // The one import a script needs
    QStringList header;
    if (!QRegularExpression(R"(^\s*(from\s+fieldes\s+import|import\s+fieldes)\b)", QRegularExpression::MultilineOption)
            .match(script).hasMatch())
    {
        header << "from fieldes import *";
    }

    // Assemble the new script: header after the last top-level import,
    // settings statements removed, the import code appended
    int after = -1;
    for (int i=0; i < lines.size(); ++i)
    {
        if (lines[i].startsWith("import ") || lines[i].startsWith("from ")) after = i;
    }
    QStringList out;
    if (after < 0) out << header;
    for (int i=0; i < lines.size(); ++i)
    {
        if (!drop[i]) out << lines[i];
        if (i == after) out << header;
    }
    while (!out.isEmpty() && out.last().trimmed().isEmpty()) out.removeLast();
    out << code;
    out << "";

    QList<TextEdit> edits;
    const int last = lines.size() - 1;
    edits << TextEdit{0, 0, last, int(lines[last].size()), out.join('\n')};
    editor->applyEdits(edits, "Import " + fi.fileName());
    view->zoomOnNextSettings();
    statusBar()->showMessage("Imported " + fi.fileName(), 8000);
}

void Window::watchImports(const QString& sceneJson)
{
    QStringList paths;
    const auto doc = QJsonDocument::fromJson(sceneJson.toUtf8()).object();
    for (const auto v : doc["items"].toArray())
    {
        const auto it = v.toObject();
        if (it["kind"].toString() != "import" || !it.contains("path")) continue;
        const QString path = QDir::cleanPath(it["path"].toString());
        if (QFileInfo::exists(path) && !paths.contains(path)) paths << path;
    }
    const QStringList had = m_stepWatcher.files();
    for (const auto& p : had) if (!paths.contains(p)) m_stepWatcher.removePath(p);
    for (const auto& p : paths) if (!had.contains(p)) m_stepWatcher.addPath(p);
}

void Window::watchBlocks()
{
    // The interpreter says which folder holds the blocks and which files are in it: the folder and the files are watched
    const QStringList lines = editor->callSupport("blocks_files", QString(), nullptr).split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty()) return;
    m_blocksFolder = lines.first();
    const QStringList want = lines;
    const QStringList had = m_blocksWatcher.files() + m_blocksWatcher.directories();
    for (const auto& p : had) if (!want.contains(p)) m_blocksWatcher.removePath(p);
    for (const auto& p : want) if (!had.contains(p) && QFileInfo::exists(p)) m_blocksWatcher.addPath(p);
}

void Window::setBlocksFolder(const QString& path)
{
    // (empty: the folder next to FielDes again)
    settings.setValue("blocks-folder", path);
    if (path.isEmpty()) qunsetenv("FIELDES_BLOCKS");
    else qputenv("FIELDES_BLOCKS", path.toLocal8Bit());
    editor->callSupport("set_blocks_folder", path, nullptr);
    watchBlocks();
    editor->onTextChangedDebounce();
}

void Window::chooseBlocksFolder()
{
    if (m_blocksFolder.isEmpty()) watchBlocks();
    const QString chosen = QFileDialog::getExistingDirectory(this, "Blocks folder: the Python files in it are your custom blocks",
                                                             m_blocksFolder);
    if (chosen.isEmpty()) return;
    setBlocksFolder(chosen);
    statusBar()->showMessage("Blocks folder: " + chosen, 6000);
}

void Window::onSceneChanged(QString json)
{
    m_lastScene = json;
    watchImports(json);
    watchBlocks();
    if (m_pendingImport.isEmpty())
    {
        return;
    }
    const QString pending = m_pendingImport;
    m_pendingImport.clear();

    const auto doc = QJsonDocument::fromJson(json.toUtf8()).object();
    for (const auto v : doc["items"].toArray())
    {
        const auto it = v.toObject();
        if (it["kind"].toString() != "import" || it["list_var"].toString() != pending)
        {
            continue;
        }
        QStringList code;
        int failed = 0;
        for (const auto pv : it["parts"].toArray())
        {
            const auto p = pv.toObject();
            if (!p["ok"].toBool()) { failed++; continue; }
            if (p.contains("var")) continue;
            // Every part of the file: there is no limit on how many are shown
            const int k = p["index"].toInt();
            code << QString("%1_%2 = %1[%2][0]").arg(pending).arg(k) << QString("%1_%2").arg(pending).arg(k);
        }
        if (failed)
        {
            code << QString("# %1 part(s) of this file could not be rebuilt; "
                            "the model tree shows why").arg(failed);
        }
        if (code.isEmpty()) return;
        const int L = it["end_line"].toInt() - 1;
        const QString t = editor->getScript().split('\n').value(L);
        editor->applyEdits({TextEdit{L, int(t.size()), L, int(t.size()), "\n" + code.join('\n')}},
                           "Show imported parts");
        return;
    }
}

// The model the guided tour works on: a new script, not a file (so that nothing of it is saved by accident)
bool Window::loadTourModel()
{
    CHECK_UNSAVED();

    QFile file(":/examples/tour.py");
    if (!file.open(QIODevice::ReadOnly)) return false;
    // (the tour is about the model tree: it is shown whatever it was before)
    for (QAction* a : menuBar()->findChildren<QAction*>())
    {
        if (a->isCheckable() && a->text() == "Model tree") a->setChecked(true);
    }
    setFilename("");
    setWindowTitle("FielDes[*]");
    editor->setScript(file.readAll(), false);
    editor->setModified(false);
    editor->guessLanguage("py");
    return true;
}

bool Window::onLoadDefault(bool)
{
    CHECK_UNSAVED();

    editor->loadDefaultScript();
    return true;
}

void Window::onQuit(bool)
{
    Window::close();
}

bool Window::reset(Language::Type t) {
    CHECK_UNSAVED();

    setFilename("");
    setWindowTitle("FielDes[*]");

    if (t != Language::LANGUAGE_NONE) {
        editor->setLanguage(t);
    }
    editor->loadDefaultScript();
    emit(setAutoload(false));
    return true;
}

bool Window::setLanguage(Language::Type t) {
    return reset(t);
}

void Window::onLanguageChanged() {
}

}   // namespace FielDes
