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
#include <algorithm>
#include <cmath>
#include <iostream>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "fieldes/appwindow.hpp"
#include "fieldes/automation.hpp"
#include "fieldes/i18n.hpp"
#include "fieldes/python/interpreter.hpp"
#include "fieldes/section.hpp"
#include "fieldes/shape.hpp"
#include "fieldes/theme.hpp"
#include "fieldes/view.hpp"
#include "libfive/run_progress.hpp"

namespace FielDes {

namespace {

// A number or a choice that the mouse wheel changes only while it has the keyboard: the wheel is for scrolling the form
class WheelGuard : public QObject
{
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (e->type() == QEvent::Wheel)
        {
            auto w = qobject_cast<QWidget*>(o);
            if (w && !w->hasFocus()) { e->ignore(); return true; }
        }
        return QObject::eventFilter(o, e);
    }
};

// The name an option goes by: the letters and digits of a label, in lower case (fieldes.app.key_of)
QString keyOf(const QString& label)
{
    QString out;
    for (const QChar c : label.toCaseFolded())
        if (c.isLetterOrNumber()) out += c;
    return out;
}

// How many digits after the point a number is written with
int digitsAfterPoint(double v)
{
    const QString s = QString::number(v, 'g', 10);
    const int dot = s.indexOf('.');
    if (dot < 0 || s.contains('e')) return 0;
    return int(s.size()) - dot - 1;
}

QString fileFilter(const QString& given)
{
    // "STEP files (*.step *.stp)", or just "*.step *.stp", or several of the first kind joined by ";;"
    if (given.trimmed().isEmpty()) return T("All files") + " (*)";
    QString f = given.trimmed();
    if (!f.contains('(')) f = T("Files") + " (" + f + ")";
    if (!f.contains("(*)")) f += ";;" + T("All files") + " (*)";
    return f;
}

QString sizeText(qint64 n)
{
    if (n < 1024) return QString("%1 bytes").arg(n);
    if (n < 1024 * 1024) return QString("%1 KB").arg(qRound(n / 1024.0));
    return QString("%1 MB").arg(n / 1024.0 / 1024.0, 0, 'f', 1);
}

// The colours of FielDes: the dark teal of the viewport's cards and the model tree (#164c5e) and a deeper one for what is typed in (#0f3a49), cream
// text (#eee8d5), Solarized's greys for what is said quietly, the blue of the selection (#268bd2) for what is pressed
const char* kMuted = "#93a1a1";
const char* kError = "#ff8f7f";
const char* kGood = "#a8cc5f";
const char* kPanel = "#164c5e";
const char* kDeep = "#0f3a49";
const char* kAccent = "#268bd2";

QPalette appPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(kPanel));
    p.setColor(QPalette::WindowText, QColor("#eee8d5"));
    p.setColor(QPalette::Base, QColor(kDeep));
    p.setColor(QPalette::AlternateBase, QColor(kPanel));
    p.setColor(QPalette::Text, QColor("#eee8d5"));
    p.setColor(QPalette::Button, QColor("#1d5b70"));
    p.setColor(QPalette::ButtonText, QColor("#eee8d5"));
    p.setColor(QPalette::BrightText, QColor("#ffffff"));
    p.setColor(QPalette::Highlight, QColor(kAccent));
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    p.setColor(QPalette::PlaceholderText, QColor("#6d8f99"));
    p.setColor(QPalette::ToolTipBase, QColor(kPanel));
    p.setColor(QPalette::ToolTipText, QColor("#eee8d5"));
    p.setColor(QPalette::Link, QColor("#5fb0ee"));
    p.setColor(QPalette::Mid, QColor("#586e75"));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor("#586e75"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#586e75"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#586e75"));
    return p;
}

QString appStyleSheet()
{
    return QString(
        "QMainWindow, QWidget#appRoot { background: %1; }"
        "QWidget { color: #eee8d5; }"
        "QLabel { background: transparent; }"
        "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"
        "QWidget#appHeader { background: %2; border-bottom: 1px solid rgba(147, 161, 161, 70); }"
        "QWidget#appBelow { border-top: 1px solid rgba(147, 161, 161, 70); }"
        "QWidget#appViewBar { background: %2; border-top: 1px solid rgba(147, 161, 161, 70); }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: %2; color: #eee8d5; border: 1px solid rgba(147, 161, 161, 90);"
        "  border-radius: 5px; padding: 4px 7px; selection-background-color: %3; selection-color: white; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %3; }"
        "QComboBox QAbstractItemView { background: %2; color: #eee8d5; border: 1px solid rgba(147, 161, 161, 100);"
        "  selection-background-color: %3; selection-color: white; outline: none; }"
        "QPushButton { background: rgba(147, 161, 161, 40); color: #eee8d5; border: 1px solid rgba(147, 161, 161, 90); border-radius: 5px;"
        "  padding: 5px 14px; }"
        "QPushButton:hover { border-color: %3; background: rgba(147, 161, 161, 70); }"
        "QPushButton:pressed { background: rgba(38, 139, 210, 150); }"
        "QPushButton:disabled { color: #586e75; border-color: rgba(147, 161, 161, 45); background: rgba(147, 161, 161, 20); }"
        "QPushButton#appRun { background: %3; color: white; border: none; border-radius: 6px; padding: 7px 24px; font-weight: 600; }"
        "QPushButton#appRun:hover { background: #3a9be0; }"
        "QPushButton#appRun:pressed { background: #1f77b4; }"
        "QPushButton#appRun[running=\"true\"] { background: #cb4b16; }"
        "QPushButton#appRun[running=\"true\"]:hover { background: #e0582a; }"
        "QPushButton#appRun:disabled { background: rgba(147, 161, 161, 50); color: #93a1a1; }"
        "QCheckBox { spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid rgba(147, 161, 161, 130); border-radius: 4px; background: %2; }"
        "QCheckBox::indicator:hover { border-color: %3; }"
        "QCheckBox::indicator:checked { background: %3; border-color: #5fb0ee; }"
        "QPlainTextEdit { background: %2; color: #eee8d5; border: 1px solid rgba(147, 161, 161, 70); border-radius: 6px; padding: 4px;"
        "  selection-background-color: %3; selection-color: white; }"
        "QProgressBar { background: rgba(147, 161, 161, 50); border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: %3; border-radius: 3px; }"
        "QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(147, 161, 161, 110); border-radius: 4px; min-height: 28px; margin: 2px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(147, 161, 161, 170); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: rgba(147, 161, 161, 110); border-radius: 4px; min-width: 28px; margin: 2px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QDialog, QMessageBox { background: %1; }"
        "QSplitter::handle { background: rgba(147, 161, 161, 70); }"
    ).arg(kPanel, kDeep, kAccent);
}

}   // anonymous namespace

AppWindow::AppWindow(const QString& path, const QStringList& presets, bool run)
    : m_autoRun(run), m_presets(presets)
{
    resize(620, 780);
    setAcceptDrops(true);
    m_documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (m_documents.isEmpty()) m_documents = QDir::homePath();
    m_documents = QDir::toNativeSeparators(m_documents);

    // ---- the window: the form above, what the run made below
    // (the FielDes colours: the palette and the style sheet of this window; its dialogs take them from it)
    setPalette(appPalette());
    setStyleSheet(appStyleSheet());
    auto central = new QWidget(this);
    central->setObjectName("appRoot");
    auto outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_header = new QWidget(central);
    m_header->setObjectName("appHeader");
    auto hl = new QVBoxLayout(m_header);
    hl->setContentsMargins(20, 16, 20, 14);
    hl->setSpacing(4);
    m_titleLabel = new QLabel(m_header);
    m_titleLabel->setStyleSheet("font-size: 19px; font-weight: 600;");
    m_descriptionLabel = new QLabel(m_header);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setStyleSheet(QString("color: %1;").arg(kMuted));
    hl->addWidget(m_titleLabel);
    hl->addWidget(m_descriptionLabel);
    outer->addWidget(m_header);

    // (the form takes the room it needs, up to a share of the screen, and scrolls beyond; what the run made has the rest)
    m_scroll = new QScrollArea(central);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_formHost = new QWidget(m_scroll);
    m_formLayout = new QVBoxLayout(m_formHost);
    m_formLayout->setContentsMargins(20, 16, 20, 8);
    m_formLayout->setSpacing(0);
    m_scroll->setWidget(m_formHost);
    outer->addWidget(m_scroll);

    auto below = new QWidget(central);
    auto bl = new QVBoxLayout(below);
    bl->setContentsMargins(20, 10, 20, 14);
    bl->setSpacing(8);

    auto controls = new QHBoxLayout();
    m_runButton = new QPushButton(T("Run"), below);
    m_runButton->setObjectName("appRun");
    m_runButton->setDefault(true);
    m_runButton->setMinimumWidth(110);
    m_runButton->setMinimumHeight(34);
    m_runButton->setEnabled(false);
    m_runButton->setToolTip(T("Run the app with the values above (Ctrl+Return)"));
    controls->addWidget(m_runButton);
    m_watchBox = new QCheckBox(T("Run again when a file changes"), below);
    m_watchBox->setToolTip(T("Watch: the app runs again by itself when a file chosen above changes, or when the app file itself is replaced"));
    controls->addWidget(m_watchBox);
    controls->addStretch(1);
    bl->addLayout(controls);

    m_bar = new QProgressBar(below);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(6);
    m_bar->hide();
    bl->addWidget(m_bar);
    m_status = new QLabel(below);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bl->addWidget(m_status);

    m_outputsBox = new QWidget(below);
    m_outputsLayout = new QVBoxLayout(m_outputsBox);
    m_outputsLayout->setContentsMargins(0, 0, 0, 0);
    m_outputsLayout->setSpacing(4);
    m_outputsBox->hide();
    bl->addWidget(m_outputsBox);

    m_log = new QPlainTextEdit(below);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(4000);
    m_log->setPlaceholderText(T("What the app prints appears here."));
    QFont mono("Inconsolata");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    m_log->setFont(mono);
    m_log->setMinimumHeight(90);
    bl->addWidget(m_log, 1);
    below->setObjectName("appBelow");
    outer->addWidget(below, 1);
    // (an app that has a viewport -- app.viewport -- gets it on the right: the form and the log stay in a column on the left)
    central->setMinimumWidth(460);
    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->setChildrenCollapsible(false);
    m_split->setHandleWidth(1);
    m_split->addWidget(central);
    m_split->setStretchFactor(0, 0);
    setCentralWidget(m_split);
    m_doc = new QTextDocument(this);

    // ---- the menu: little is needed
    auto file = menuBar()->addMenu(T("&File"));
    auto open = file->addAction(T("Open app..."));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, &AppWindow::onOpen);
    auto reload = file->addAction(T("Read the app again"));
    reload->setShortcut(Qt::Key_F5);
    connect(reload, &QAction::triggered, this, [this] { if (!m_running && !m_path.isEmpty()) openApp(m_path, true); });
    file->addSeparator();
    auto quit = file->addAction(T("Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto runAction = new QAction(this);
    runAction->setShortcut(Qt::CTRL + Qt::Key_Return);
    addAction(runAction);
    connect(runAction, &QAction::triggered, this, [this] { if (m_running) return; onRun(); });

    connect(m_runButton, &QPushButton::clicked, this, [this] { if (m_running) onStop(); else onRun(); });
    m_poll.setInterval(150);
    connect(&m_poll, &QTimer::timeout, this, &AppWindow::onPoll);
    m_liveTimer.setSingleShot(true);
    connect(&m_liveTimer, &QTimer::timeout, this, &AppWindow::onLiveTimer);
    m_watchDebounce.setSingleShot(true);
    m_watchDebounce.setInterval(800);
    connect(&m_watchDebounce, &QTimer::timeout, this, &AppWindow::onWatchTriggered);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &AppWindow::onWatchedChanged);

    // ---- Python: started here, so that the app can be read; the interpreter thread follows
    m_interp = new Python::Interpreter();
    m_interp->preinit();
    m_open = openApp(path, false);

    m_interp->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_interp, &::FielDes::Interpreter::init);
    connect(m_interp, &::FielDes::Interpreter::ready, this, &AppWindow::onReady);
    connect(m_interp, &::FielDes::Interpreter::done, this, &AppWindow::onDone);
    connect(this, &AppWindow::evalRequested, m_interp, &::FielDes::Interpreter::eval);
    m_thread.start();
    if (m_open) setStatus(T("Starting..."), kMuted);

    registerAutomation();
}

AppWindow::~AppWindow()
{
    m_poll.stop();
    if (m_interp)
    {
        m_interp->halt();
        m_thread.quit();
        m_thread.wait();
        delete m_interp;
        m_interp = nullptr;
    }
}

QString AppWindow::callSupport(const QString& function, const QJsonObject& arg, QString* error)
{
    return m_interp->callSupport(function, QString::fromUtf8(QJsonDocument(arg).toJson(QJsonDocument::Compact)), error);
}

void AppWindow::showProblem(const QString& title, const QString& text)
{
    QMessageBox::warning(this, title, text);
}

bool AppWindow::openApp(const QString& path, bool again)
{
    QString error;
    const QString text = callSupport("app_open", QJsonObject{{"path", path}}, &error);
    const auto doc = QJsonDocument::fromJson(text.toUtf8()).object();
    if (!doc["ok"].toBool())
    {
        const QString why = doc["error"].toString().isEmpty() ? error : doc["error"].toString();
        if (again)
        {
            // (the file was being written, or is gone: what is open stays, and the log says so)
            log(T("The app could not be read again: %1").arg(why), kError);
            return false;
        }
        showProblem(T("This app cannot be opened"), why);
        return false;
    }
    m_path = doc["path"].toString();
    m_kind = doc["kind"].toString();
    m_script = doc["script"].toString();
    m_originalScript = m_script;
    m_doc->setPlainText(m_script);
    m_varSpans.clear();
    m_manifest = doc["manifest"].toObject();
    m_hasViewport = m_manifest["viewport"].isObject();
    if (!doc["documents"].toString().isEmpty() && m_documents.isEmpty()) m_documents = doc["documents"].toString();
    m_id = m_manifest["id"].toString();
    m_title = m_manifest["title"].toString();
    const QString version = m_manifest["version"].toString();
    setWindowTitle(m_title + (m_kind == "script" ? "  (" + T("preview") + ")" : QString()) + " - FielDes");
    m_titleLabel->setText(m_title + (version.isEmpty() ? QString() : "  " + QString("<span style='font-size:12px; font-weight:400; color:%1'>%2</span>").arg(kMuted, version)));
    m_titleLabel->setTextFormat(Qt::RichText);
    const QString description = m_manifest["description"].toString();
    m_descriptionLabel->setText(description);
    m_descriptionLabel->setVisible(!description.isEmpty());
    for (const auto p : doc["problems"].toArray())
    {
        const auto o = p.toObject();
        log(T("Line %1: %2").arg(o["line"].toInt()).arg(o["message"].toString()), kError);
    }
    buildForm(again);
    ensureViewport();
    if (m_view && m_resetButton) m_resetButton->hide();
    if (again && m_hasViewport) scheduleLive(100);
    if (!m_path.isEmpty() && !m_watcher.files().contains(m_path)) m_watcher.addPath(m_path);
    if (again) setStatus(T("The app was read again."), kMuted);
    return true;
}

AppWindow::Field* AppWindow::field(const QString& key)
{
    for (auto& f : m_fields)
        if (f.key == key) return &f;
    return nullptr;
}

void AppWindow::buildForm(bool again)
{
    const QJsonObject keep = again ? values() : QJsonObject();
    m_building = true;
    m_fields.clear();
    if (m_form)
    {
        m_formLayout->removeWidget(m_form);
        delete m_form;
        m_form = nullptr;
    }
    m_form = new QWidget(m_formHost);
    auto layout = new QVBoxLayout(m_form);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);
    static WheelGuard* guard = new WheelGuard(qApp);

    const auto inputs = m_manifest["inputs"].toArray();
    for (const auto& iv : inputs)
    {
        const auto spec = iv.toObject();
        Field f;
        f.spec = spec;
        f.key = spec["key"].toString();
        f.label = spec["label"].toString();
        f.kind = spec["kind"].toString();
        const QString help = spec["help"].toString();

        auto row = new QWidget(m_form);
        auto rl = new QVBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(3);
        auto addLabel = [&] {
            auto l = new QLabel(f.label, row);
            l->setStyleSheet("font-weight: 600;");
            rl->addWidget(l);
        };

        if (f.kind == "number")
        {
            addLabel();
            const bool integer = spec["integer"].toBool();
            const double def = spec["default"].toDouble();
            const bool hasMin = spec["min"].isDouble(), hasMax = spec["max"].isDouble(), hasStep = spec["step"].isDouble();
            const QString unit = spec["unit"].toString();
            if (integer)
            {
                auto sb = new QSpinBox(row);
                sb->setRange(hasMin ? int(spec["min"].toDouble()) : -1000000000, hasMax ? int(spec["max"].toDouble()) : 1000000000);
                sb->setSingleStep(hasStep ? std::max(1, int(spec["step"].toDouble())) : 1);
                sb->setValue(int(def));
                if (!unit.isEmpty()) sb->setSuffix(" " + unit);
                sb->setFocusPolicy(Qt::StrongFocus);
                sb->installEventFilter(guard);
                sb->setMinimumWidth(150);
                f.editor = sb;
                f.get = [sb] { return QJsonValue(sb->value()); };
                f.set = [sb](const QJsonValue& v) { sb->setValue(v.isString() ? v.toString().toInt() : int(std::lround(v.toDouble()))); };
            }
            else
            {
                auto sb = new QDoubleSpinBox(row);
                int decimals = std::max({digitsAfterPoint(def), hasStep ? digitsAfterPoint(spec["step"].toDouble()) : 0,
                                         hasMin ? digitsAfterPoint(spec["min"].toDouble()) : 0, hasMax ? digitsAfterPoint(spec["max"].toDouble()) : 0, 2});
                decimals = std::min(decimals, 6);
                sb->setDecimals(decimals);
                sb->setRange(hasMin ? spec["min"].toDouble() : -1e9, hasMax ? spec["max"].toDouble() : 1e9);
                sb->setSingleStep(hasStep ? spec["step"].toDouble() : std::max(0.01, std::pow(10.0, std::floor(std::log10(std::max(std::abs(def), 1e-9))) - 1)));
                sb->setValue(def);
                if (!unit.isEmpty()) sb->setSuffix(" " + unit);
                sb->setFocusPolicy(Qt::StrongFocus);
                sb->installEventFilter(guard);
                sb->setMinimumWidth(150);
                f.editor = sb;
                f.get = [sb] { return QJsonValue(sb->value()); };
                f.set = [sb](const QJsonValue& v) { sb->setValue(v.isString() ? v.toString().replace(',', '.').toDouble() : v.toDouble()); };
            }
            auto line = new QHBoxLayout();
            line->addWidget(f.editor);
            line->addStretch(1);
            rl->addLayout(line);
        }
        else if (f.kind == "flag")
        {
            auto cb = new QCheckBox(f.label, row);
            cb->setStyleSheet("font-weight: 600;");
            cb->setChecked(spec["default"].toBool());
            f.editor = cb;
            f.get = [cb] { return QJsonValue(cb->isChecked()); };
            f.set = [cb](const QJsonValue& v) {
                if (v.isString()) { const QString s = v.toString().toLower(); cb->setChecked(s == "1" || s == "true" || s == "yes" || s == "on"); }
                else cb->setChecked(v.toBool());
            };
            rl->addWidget(cb);
        }
        else if (f.kind == "choice")
        {
            addLabel();
            auto combo = new QComboBox(row);
            for (const auto& o : spec["options"].toArray()) combo->addItem(o.toString());
            combo->setCurrentText(spec["default"].toString());
            combo->setFocusPolicy(Qt::StrongFocus);
            combo->installEventFilter(guard);
            combo->setMinimumWidth(220);
            f.editor = combo;
            f.get = [combo] { return QJsonValue(combo->currentText()); };
            f.set = [combo](const QJsonValue& v) {
                const int i = combo->findText(v.toString(), Qt::MatchFixedString);
                if (i >= 0) combo->setCurrentIndex(i);
            };
            auto line = new QHBoxLayout();
            line->addWidget(combo);
            line->addStretch(1);
            rl->addLayout(line);
        }
        else if (f.kind == "text")
        {
            addLabel();
            auto edit = new QLineEdit(spec["default"].toString(), row);
            f.editor = edit;
            f.get = [edit] { return QJsonValue(edit->text()); };
            f.set = [edit](const QJsonValue& v) { edit->setText(v.toString()); };
            rl->addWidget(edit);
        }
        else    // file, save_as, folder: a path, and a button to choose it
        {
            addLabel();
            QString def = spec["default"].isString() ? spec["default"].toString() : QString();
            if ((f.kind == "save_as" || f.kind == "folder") && !def.isEmpty() && QDir::isRelativePath(def))
                def = QDir::toNativeSeparators(QDir(m_documents).filePath(def));
            else if (!def.isEmpty())
                def = QDir::toNativeSeparators(def);
            auto edit = new QLineEdit(def, row);
            if (f.kind == "file") edit->setPlaceholderText(T("Choose a file... (or drop it on this window)"));
            auto button = new QPushButton(f.kind == "file" ? T("Browse...") : f.kind == "save_as" ? T("Save as...") : T("Choose..."), row);
            const QString filter = spec["filter"].toString();
            const QString kind = f.kind;
            connect(button, &QPushButton::clicked, this, [this, edit, kind, filter] {
                const QString current = edit->text().trimmed();
                const QString start = current.isEmpty() ? m_documents : current;
                QString chosen;
                if (kind == "file") chosen = QFileDialog::getOpenFileName(this, T("Choose a file"), start, fileFilter(filter));
                else if (kind == "save_as") chosen = QFileDialog::getSaveFileName(this, T("Save as"), start, fileFilter(filter));
                else chosen = QFileDialog::getExistingDirectory(this, T("Choose a folder"), QFileInfo(start).isDir() ? start : m_documents);
                if (!chosen.isEmpty()) edit->setText(QDir::toNativeSeparators(chosen));
            });
            auto line = new QHBoxLayout();
            line->addWidget(edit, 1);
            line->addWidget(button);
            rl->addLayout(line);
            f.editor = edit;
            f.get = [edit] { return QJsonValue(edit->text().trimmed()); };
            f.set = [edit](const QJsonValue& v) { edit->setText(QDir::toNativeSeparators(v.toString())); };
        }
        if (!help.isEmpty())
        {
            auto h = new QLabel(help, row);
            h->setWordWrap(true);
            h->setStyleSheet(QString("color: %1; font-size: 12px;").arg(kMuted));
            rl->addWidget(h);
        }
        f.error = new QLabel(row);
        f.error->setWordWrap(true);
        f.error->setStyleSheet(QString("color: %1; font-size: 12px;").arg(kError));
        f.error->hide();
        rl->addWidget(f.error);
        layout->addWidget(row);
        m_fields.append(f);
        // (with a viewport, a change of an input makes the picture again, a moment after the last one)
        if (qobject_cast<QSpinBox*>(f.editor)) connect(static_cast<QSpinBox*>(f.editor), QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { onInputEdited(); });
        else if (qobject_cast<QDoubleSpinBox*>(f.editor)) connect(static_cast<QDoubleSpinBox*>(f.editor), QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { onInputEdited(); });
        else if (qobject_cast<QCheckBox*>(f.editor)) connect(static_cast<QCheckBox*>(f.editor), &QCheckBox::toggled, this, [this] { onInputEdited(); });
        else if (qobject_cast<QComboBox*>(f.editor)) connect(static_cast<QComboBox*>(f.editor), QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { onInputEdited(); });
        else if (qobject_cast<QLineEdit*>(f.editor)) connect(static_cast<QLineEdit*>(f.editor), &QLineEdit::textChanged, this, [this] { onInputEdited(); });
    }
    if (m_fields.isEmpty())
    {
        auto none = new QLabel(T("This app has nothing to choose: press Run."), m_form);
        none->setStyleSheet(QString("color: %1;").arg(kMuted));
        layout->addWidget(none);
    }
    layout->addStretch(1);
    m_formLayout->addWidget(m_form);
    {
        const int room = int(0.55 * (screen() ? screen()->availableGeometry().height() : 900));
        m_scroll->setMinimumHeight(std::min(m_form->sizeHint().height() + 28, room));
    }
    // (what was typed before the form was read again stays, where the input is still there; then the last run's values; then the command line)
    applySaved();
    for (auto it = keep.begin(); it != keep.end(); ++it)
        if (auto f = field(it.key())) f->set(it.value());
    applyPresets();
    m_building = false;
}

QJsonObject AppWindow::values() const
{
    QJsonObject out;
    for (const auto& f : m_fields) out[f.key] = f.get();
    return out;
}

void AppWindow::applySaved()
{
    // (a test run of the program does not read or write the user's settings)
    if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION")) return;
    QSettings s;
    const auto saved = QJsonDocument::fromJson(s.value("apps/" + m_id + "/values").toString().toUtf8()).object();
    for (auto it = saved.begin(); it != saved.end(); ++it)
    {
        auto f = field(it.key());
        // (a path that is not there any more is not put back: the default is better than a file that is gone)
        if (!f || f->kind == "file") continue;
        f->set(it.value());
    }
    m_watchBox->setChecked(s.value("apps/" + m_id + "/watch", false).toBool());
}

void AppWindow::saveValues()
{
    if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION")) return;
    QSettings s;
    QJsonObject keep;
    for (const auto& f : m_fields)
        if (f.kind != "file") keep[f.key] = f.get();
    s.setValue("apps/" + m_id + "/values", QString::fromUtf8(QJsonDocument(keep).toJson(QJsonDocument::Compact)));
    s.setValue("apps/" + m_id + "/watch", m_watchBox->isChecked());
}

void AppWindow::applyPresets()
{
    for (int i = 0; i < m_presets.size(); ++i)
    {
        QString token = m_presets[i];
        if (!token.startsWith("--")) continue;
        token = token.mid(2);
        QString value;
        bool hasValue = false;
        const int eq = token.indexOf('=');
        if (eq >= 0) { value = token.mid(eq + 1); token = token.left(eq); hasValue = true; }
        const QString low = token.toLower();
        if (low == "run" || low == "watch") continue;
        bool negate = false;
        Field* f = field(keyOf(token));
        if (!f && (low.startsWith("no-") || low.startsWith("no_")))
        {
            f = field(keyOf(token.mid(3)));
            negate = f && f->kind == "flag";
            if (!negate) f = nullptr;
        }
        if (!f)
        {
            log(T("This app has no input called \"%1\".").arg(token), kError);
            continue;
        }
        if (negate) { f->set(QJsonValue(false)); continue; }
        if (!hasValue)
        {
            if (f->kind == "flag" && (i + 1 >= m_presets.size() || m_presets[i + 1].startsWith("--"))) { f->set(QJsonValue(true)); continue; }
            if (i + 1 < m_presets.size()) value = m_presets[++i];
        }
        f->set(QJsonValue(value));
    }
}

void AppWindow::setError(const QString& key, const QString& why)
{
    if (auto f = field(key))
    {
        f->error->setText(why);
        f->error->show();
    }
}

void AppWindow::clearErrors()
{
    for (auto& f : m_fields) f.error->hide();
}

void AppWindow::setStatus(const QString& text, const QString& color)
{
    m_status->setText(text);
    m_status->setStyleSheet(color.isEmpty() ? QString() : QString("color: %1;").arg(color));
}

void AppWindow::log(const QString& text, const QString& color)
{
    QTextCharFormat format;
    if (!color.isEmpty()) format.setForeground(QColor(color));
    QTextCursor c = m_log->textCursor();
    c.movePosition(QTextCursor::End);
    if (!m_log->document()->isEmpty()) c.insertText("\n");
    c.insertText(text, format);
    m_log->setTextCursor(c);
    m_log->ensureCursorVisible();
}

void AppWindow::setRunning(bool running)
{
    m_running = running;
    m_runButton->setText(running ? T("Stop") : T("Run"));
    m_runButton->setProperty("running", running);
    m_runButton->style()->unpolish(m_runButton);
    m_runButton->style()->polish(m_runButton);
    m_runButton->setEnabled(running || m_ready);
    if (m_form) m_form->setEnabled(!running);
    m_bar->setVisible(running);
    if (running) { m_bar->setRange(0, 0); m_poll.start(); } else m_poll.stop();
}

void AppWindow::onReady(QStringList, Documentation)
{
    m_ready = true;
    m_runButton->setEnabled(m_open && !m_running);
    if (!m_open) return;
    setStatus(QString(), kMuted);
    if (m_autoRun) { m_autoRun = false; QTimer::singleShot(0, this, &AppWindow::onRun); }
    else if (m_hasViewport) scheduleLive(0);
}

void AppWindow::onRun()
{
    if (!m_ready || m_running || !m_open) return;
    if (m_picture)
    {
        // (the picture that is being made is stopped, and the run follows: the person has asked for the work)
        m_runAfter = true;
        m_stopped = true;
        m_interp->terminate();
        return;
    }
    m_liveTimer.stop();
    m_liveAgain = false;
    clearErrors();
    QString error;
    const QString text = callSupport("app_check", QJsonObject{{"values", values()}, {"outputs", m_documents}}, &error);
    const auto res = QJsonDocument::fromJson(text.toUtf8()).object();
    if (!res["ok"].toBool())
    {
        const auto errors = res["errors"].toObject();
        QString first;
        for (auto it = errors.begin(); it != errors.end(); ++it)
        {
            setError(it.key(), it.value().toString());
            if (first.isEmpty()) first = it.key();
        }
        if (auto f = field(first)) f->editor->setFocus();
        if (errors.isEmpty()) log(res["error"].toString().isEmpty() ? error : res["error"].toString(), kError);
        setStatus(T("Check the inputs marked in red."), kError);
        return;
    }
    saveValues();
    QStringList watch;
    for (const auto& w : res["watch"].toArray()) watch << w.toString();
    updateWatch(watch);
    m_stopped = false;
    if (!m_log->document()->isEmpty()) log(QString());
    log(QString("--- %1 ---").arg(QTime::currentTime().toString("HH:mm:ss")) + (m_changed.isEmpty() ? QString() : "  " + m_changed), kMuted);
    m_changed.clear();
    setStatus(T("Running..."), kMuted);
    while (m_outputsLayout->count())
    {
        auto item = m_outputsLayout->takeAt(0);
        delete item->widget();
        delete item;
    }
    m_outputsBox->hide();
    setRunning(true);
    m_clock.start();
    m_interp->noteRequest();
    emit evalRequested(m_script);
}

void AppWindow::onStop()
{
    if (!m_running) return;
    m_stopped = true;
    setStatus(T("Stopping..."), kMuted);
    m_interp->terminate();
}

void AppWindow::onPoll()
{
    const auto r = libfive::run_progress::current();
    if (m_picture)
    {
        // (a picture shows how far it is on the bar only: the status line keeps what the last run said)
        if (r.step < 0 || r.steps <= 0) { m_bar->setRange(0, 0); return; }
        m_bar->setRange(0, 1000 * r.steps);
        m_bar->setValue(int(1000 * (r.step + (r.fraction >= 0 ? std::min(1.0, r.fraction) : 0.0))));
        return;
    }
    if (r.step < 0 || r.steps <= 0)
    {
        m_bar->setRange(0, 0);
        return;
    }
    m_bar->setRange(0, 1000 * r.steps);
    const double inStep = r.fraction >= 0 ? std::min(1.0, r.fraction) : 0.0;
    m_bar->setValue(int(1000 * (r.step + inStep)));
    QString what = QString::fromStdString(r.detail);
    setStatus(T("Running...") + QString("  %1 / %2").arg(r.step + 1).arg(r.steps) + (what.isEmpty() ? QString() : "  ·  " + what), kMuted);
}

void AppWindow::onDone(Result r)
{
    const double seconds = m_clock.elapsed() / 1000.0;
    const bool terminated = m_stopped && !r.okay;
    if (m_picture)
    {
        m_interp->callSupport("app_end", "", nullptr);
        finishPicture(r, terminated);
        return;
    }
    showInViewport(r, terminated);
    setRunning(false);
    m_interp->callSupport("app_end", "", nullptr);
    if (m_closing)
    {
        close();
        return;
    }
    if (r.replaced || terminated)
    {
        log(T("Stopped."), kMuted);
        setStatus(T("Stopped."), kMuted);
    }
    else if (r.okay)
    {
        const QString printed = r.result.trimmed();
        if (!printed.isEmpty() && printed != "None") log(printed);
        setStatus(T("Done in %1 s.").arg(seconds, 0, 'f', seconds < 10 ? 1 : 0), kGood);
        // (an input written in a loop or a function cannot be on the form: the engineer is told)
        QString error;
        const auto missing = QJsonDocument::fromJson(m_interp->callSupport("app_undeclared", "", &error).toUtf8()).array();
        if (!missing.isEmpty())
        {
            QStringList names;
            for (const auto& m : missing) names << m.toString();
            log(T("These inputs are declared where the form cannot read them (in a loop or a function), so they are not on it: %1. Write app.number(...) and the others at the top of the script.").arg(names.join(", ")), kError);
        }
    }
    else
    {
        const QString tb = r.error.error.trimmed();
        if (!r.result.trimmed().isEmpty() && r.result.trimmed() != "None") log(r.result.trimmed());
        log(tb, kError);
        QString last;
        for (const auto& line : tb.split('\n', Qt::SkipEmptyParts)) last = line.trimmed();
        setStatus(T("The app stopped with an error: %1").arg(last), kError);
    }
    showOutputs();

    if (m_reloadAfter)
    {
        m_reloadAfter = false;
        openApp(m_path, true);
        if (m_watchBox->isChecked()) m_runAgain = true;
    }
    if (m_runAgain)
    {
        m_runAgain = false;
        QTimer::singleShot(300, this, &AppWindow::onRun);
    }
}

void AppWindow::showOutputs()
{
    QString error;
    const auto outputs = QJsonDocument::fromJson(m_interp->callSupport("app_outputs", "", &error).toUtf8()).array();
    while (m_outputsLayout->count())
    {
        auto item = m_outputsLayout->takeAt(0);
        delete item->widget();
        delete item;
    }
    if (outputs.isEmpty())
    {
        m_outputsBox->hide();
        return;
    }
    auto heading = new QLabel(T("Outputs"), m_outputsBox);
    heading->setStyleSheet("font-weight: 600;");
    m_outputsLayout->addWidget(heading);
    for (const auto& ov : outputs)
    {
        const auto o = ov.toObject();
        const QString path = o["path"].toString();
        const bool exists = o["exists"].toBool(), fresh = o["fresh"].toBool();
        auto row = new QWidget(m_outputsBox);
        auto rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        QString text = QString("<b>%1</b> &nbsp;<span style='color:%2'>%3</span>").arg(o["label"].toString().toHtmlEscaped(), kMuted, QDir::toNativeSeparators(path).toHtmlEscaped());
        if (!exists) text += QString(" &nbsp;<span style='color:%1'>%2</span>").arg(kError, T("not written"));
        else text += QString(" &nbsp;<span style='color:%1'>%2%3</span>").arg(kMuted, sizeText(o["size"].toDouble()), fresh ? QString() : " - " + T("from an earlier run"));
        auto label = new QLabel(text, row);
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rl->addWidget(label, 1);
        if (exists)
        {
            auto openBtn = new QPushButton(T("Open"), row);
            connect(openBtn, &QPushButton::clicked, this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
            auto showBtn = new QPushButton(T("Show in folder"), row);
            connect(showBtn, &QPushButton::clicked, this, [path] {
#ifdef Q_OS_WIN
                QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
#else
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
            });
            rl->addWidget(openBtn);
            rl->addWidget(showBtn);
        }
        m_outputsLayout->addWidget(row);
    }
    m_outputsBox->show();
}

void AppWindow::updateWatch(const QStringList& files)
{
    const auto old = m_watcher.files();
    if (!old.isEmpty()) m_watcher.removePaths(old);
    QStringList wanted = files;
    if (!m_path.isEmpty() && !wanted.contains(m_path)) wanted << m_path;
    for (const auto& f : wanted)
        if (QFileInfo::exists(f)) m_watcher.addPath(f);
}

void AppWindow::onWatchedChanged(const QString& path)
{
    // (a program that saves a file by replacing it makes the watcher forget it: it is watched again)
    QTimer::singleShot(400, this, [this, path] { if (QFileInfo::exists(path) && !m_watcher.files().contains(path)) m_watcher.addPath(path); });
    m_changed = T("%1 changed").arg(QFileInfo(path).fileName());
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(m_path).absoluteFilePath()) m_changed = T("the app was replaced");
    m_watchDebounce.start();
}

void AppWindow::onWatchTriggered()
{
    const bool isApp = m_changed == T("the app was replaced");
    if (m_running)
    {
        if (isApp) m_reloadAfter = true;
        else if (m_watchBox->isChecked()) m_runAgain = true;
        return;
    }
    if (isApp)
    {
        // (the app file was replaced: the form is read again, whatever Watch says; Watch runs it)
        const QString what = m_changed;
        if (openApp(m_path, true) && m_watchBox->isChecked())
        {
            m_changed = what;
            onRun();
        }
        else m_changed.clear();
        return;
    }
    if (m_watchBox->isChecked()) onRun();
    else m_changed.clear();
}

void AppWindow::onOpen()
{
    if (m_running) return;
    const QString f = QFileDialog::getOpenFileName(this, T("Open app"), QFileInfo(m_path).absolutePath(),
                                                   T("FielDes apps") + " (*.fieldesapp);;" + T("FielDes scripts") + " (*.py)");
    if (f.isEmpty()) return;
    // (a new app is a new process: this window keeps its interpreter, which has the app it was started with)
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {f});
}

void AppWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() && !m_running) event->acceptProposedAction();
}

void AppWindow::dropEvent(QDropEvent* event)
{
    // A file dropped on the window goes into the first input that reads a file, the one whose filter has its extension first
    if (m_running) return;
    for (const auto& url : event->mimeData()->urls())
    {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        const QString ext = "*." + QFileInfo(path).suffix().toLower();
        Field* target = nullptr;
        for (auto& f : m_fields)
        {
            if (f.kind != "file") continue;
            if (!target) target = &f;
            if (f.spec["filter"].toString().toLower().contains(ext)) { target = &f; break; }
        }
        if (target)
        {
            target->set(QJsonValue(path));
            event->acceptProposedAction();
            return;
        }
    }
}

void AppWindow::closeEvent(QCloseEvent* event)
{
    if (m_picture)
    {
        m_closing = true;
        m_stopped = true;
        m_interp->terminate();
        event->ignore();
        return;
    }
    if (m_running)
    {
        if (QMessageBox::question(this, windowTitle(), T("The app is still running. Stop it and close the window?")) != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        m_closing = true;
        m_stopped = true;
        m_interp->terminate();
        event->ignore();
        return;
    }
    event->accept();
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// The viewport (app.viewport)

void AppWindow::ensureViewport()
{
    if (!m_hasViewport)
    {
        if (m_viewHost) m_viewHost->hide();
        return;
    }
    if (!m_viewHost)
    {
        m_viewHost = new QWidget(m_split);
        auto vl = new QVBoxLayout(m_viewHost);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);
        m_view = new View(m_viewHost);
        m_view->setAppMode(true);
        m_view->setUpAxis(QSettings().value("up-axis", "z").toString() == "y");
        vl->addWidget(m_view, 1);
        auto bar = new QWidget(m_viewHost);
        bar->setObjectName("appViewBar");
        auto bl = new QHBoxLayout(bar);
        bl->setContentsMargins(14, 7, 14, 7);
        m_viewHint = new QLabel(T("Click a model to select it. A model that is not locked can be dragged by its gizmo and by its surfaces: the numbers are used when you press Run."), bar);
        m_viewHint->setWordWrap(true);
        m_viewHint->setStyleSheet(QString("color: %1; font-size: 12px;").arg(kMuted));
        bl->addWidget(m_viewHint, 1);
        m_resetButton = new QPushButton(T("Reset the picture"), bar);
        m_resetButton->setToolTip(T("Take back what was dragged in the viewport: the numbers of the app's script are what it says again"));
        m_resetButton->hide();
        connect(m_resetButton, &QPushButton::clicked, this, &AppWindow::onResetPicture);
        bl->addWidget(m_resetButton);
        vl->addWidget(bar);
        m_split->addWidget(m_viewHost);
        m_split->setStretchFactor(1, 1);
        resize(1320, 840);
        m_split->setSizes({560, 760});

        // The fields: an icon for each (with its name over it); a click on it opens the viewer of the field -- a disc of it --
        // and the card edits that viewer
        m_fieldPanel = new FieldPanel(m_view);
        m_fieldPanel->hide();
        connect(m_fieldPanel, &FieldPanel::settingsChanged, m_view, &View::setFieldView);
        connect(m_view, &View::fieldMoved, m_fieldPanel, &FieldPanel::setCentre);
        connect(m_view, &View::fieldSliceReady, m_fieldPanel, &FieldPanel::setSlice);
        connect(m_view, &View::fieldReadout, m_fieldPanel, &FieldPanel::setReadout);
        connect(m_fieldPanel, &FieldPanel::fieldChosen, m_view, &View::setActiveField);
        connect(m_view, &View::fieldViewersChanged, this, [this] {
            const QStringList keys = m_view->openFieldKeys();
            if (keys.isEmpty()) { m_fieldPanel->hide(); return; }
            QStringList names;
            for (const QString& k : keys) names << (k.startsWith("line:") ? T("field at line %1").arg(k.mid(5)) : k);
            const QString active = keys.contains(m_view->activeField()) ? m_view->activeField() : keys.first();
            m_fieldPanel->setFields(keys, names, std::max(0, int(keys.indexOf(active))));
            FieldViewSettings s;
            QVector3D lo, hi;
            if (m_view->fieldViewer(active, s, lo, hi)) m_fieldPanel->setViewer(active, s, lo, hi);
            m_fieldPanel->show();
            m_fieldPanel->raise();
        });
        connect(m_view, &View::fieldIconClicked, this, [this](QString key, bool ctrl) {
            if (ctrl) { if (m_openFields.contains(key)) m_openFields.removeAll(key); else m_openFields << key; }
            else if (m_openFields == QStringList{key}) m_openFields.clear();
            else m_openFields = QStringList{key};
            if (!ctrl && !m_selected.isEmpty()) { m_selected.clear(); applySelection(); }       // (one selection: a field and a model are not selected together)
            m_view->setOpenFields(m_openFields);
        });
        connect(m_view, &View::shapeClicked, this, [this](int line) {
            m_selected.clear();
            if (line >= 0) m_selected.insert(line);
            applySelection();
            if (!m_openFields.isEmpty()) { m_openFields.clear(); m_view->setOpenFields(m_openFields); }
        });
        connect(m_view, &View::fieldViewersChanged, this, [this] { m_openFields = m_view->openFieldKeys(); });
        connect(m_view, &View::shapeToggled, this, [this](int line) {
            if (m_selected.contains(line)) m_selected.remove(line); else m_selected.insert(line);
            applySelection();
        });
        connect(m_view, &View::shapesRectSelected, this, [this](QList<int> lines, bool add) {
            if (!add) m_selected.clear();
            for (int l : lines) m_selected.insert(l);
            applySelection();
        });
        connect(m_view, &View::fieldsRectSelected, this, [this](QStringList keys, bool add) {
            if (add) { for (const QString& k : keys) if (!m_openFields.contains(k)) m_openFields << k; }
            else m_openFields = keys;
            if (!add && !m_selected.isEmpty()) { m_selected.clear(); applySelection(); }
            m_view->setOpenFields(m_openFields);
        });
        connect(m_view, &View::dragStart, this, [this] { m_dragging = true; });
        connect(m_view, &View::varsDragged, this, [this](QMap<libfive::Tree::Id, float> vs) { applyVarValues(vs); });
        connect(m_view, &View::dragEnd, this, [this] {
            m_dragging = false;
            // (what was dragged is in the text: it is what the app runs now, and the picture is made again from it)
            m_script = m_doc->toPlainText();
            if (m_resetButton) m_resetButton->setVisible(m_script != m_originalScript);
            scheduleLive(250);
        });
    }
    m_viewHost->show();
}

void AppWindow::applySelection()
{
    if (m_view) m_view->highlightLines(m_selected.values());
}

void AppWindow::onInputEdited()
{
    if (m_building || !m_hasViewport || !m_open) return;
    scheduleLive(700);
}

void AppWindow::scheduleLive(int ms)
{
    if (!m_hasViewport || !m_open) return;
    m_liveTimer.start(ms);
}

void AppWindow::onLiveTimer()
{
    if (!m_ready || !m_hasViewport || !m_open) return;
    // (while a model is dragged the text is not whole yet; while the app runs or a picture is made, the next picture waits for it)
    if (m_dragging) { m_liveTimer.start(300); return; }
    if (m_running || m_picture) { m_liveAgain = true; return; }
    startPicture();
}

void AppWindow::setPicture(bool on)
{
    m_picture = on;
    m_bar->setVisible(on);
    if (on) { m_bar->setRange(0, 0); m_poll.start(); } else if (!m_running) m_poll.stop();
}

void AppWindow::startPicture()
{
    clearErrors();
    QString error;
    const QString text = callSupport("app_check", QJsonObject{{"values", values()}, {"outputs", m_documents}, {"live", true}}, &error);
    const auto res = QJsonDocument::fromJson(text.toUtf8()).object();
    if (!res["ok"].toBool())
    {
        // (an input that is not filled in yet is no reason to scold: the picture waits for it)
        const auto errors = res["errors"].toObject();
        QStringList labels;
        for (auto it = errors.begin(); it != errors.end(); ++it)
            if (auto f = field(it.key())) labels << f->label;
        if (errors.isEmpty()) log(res["error"].toString().isEmpty() ? error : res["error"].toString(), kError);
        setStatus(T("The picture is made when these inputs are right: %1").arg(labels.join(", ")), kMuted);
        return;
    }
    m_stopped = false;
    m_clock.start();
    setPicture(true);
    m_interp->noteRequest();
    emit evalRequested(m_script);
}

void AppWindow::finishPicture(Result& r, bool terminated)
{
    setPicture(false);
    showInViewport(r, terminated);
    if (!(r.replaced || terminated))
    {
        if (r.okay)
        {
            m_lastPictureError.clear();
            if (m_pictureError) { m_pictureError = false; setStatus(QString(), kMuted); }
        }
        else
        {
            const QString tb = r.error.error.trimmed();
            QString last;
            for (const auto& line : tb.split('\n', Qt::SkipEmptyParts)) last = line.trimmed();
            // (the same error again and again, as an input is typed, is said once)
            if (last != m_lastPictureError) { log(tb, kError); m_lastPictureError = last; }
            setStatus(T("The picture could not be made: %1").arg(last), kError);
            m_pictureError = true;
        }
    }
    if (m_closing) { close(); return; }
    if (m_runAfter)
    {
        m_runAfter = false;
        m_stopped = false;
        QTimer::singleShot(0, this, &AppWindow::onRun);
        return;
    }
    if (m_reloadAfter)
    {
        m_reloadAfter = false;
        openApp(m_path, true);
        if (m_watchBox->isChecked()) m_runAgain = true;
    }
    if (m_runAgain) { m_runAgain = false; QTimer::singleShot(300, this, &AppWindow::onRun); return; }
    if (m_liveAgain) { m_liveAgain = false; scheduleLive(100); }
}

// What a run made goes to the viewport: the models the script gave app.viewport, each as a shape with the line it is selected by; the others
// the script displays (the editor shows them) are not there
void AppWindow::showInViewport(Result& r, bool terminated)
{
    if (!m_hasViewport || !m_view || r.replaced || terminated)
    {
        for (auto s : r.shapes) s->deleteLater();
        return;
    }
    QList<Shape*> mine;
    for (auto s : r.shapes)
    {
        if (s->appShown()) mine << s;
        else s->deleteLater();
    }
    if (!r.okay)
    {
        // (a run that failed before it got to app.viewport has no picture: the one that is there stays, with the error over it)
        QString last;
        for (const auto& line : r.error.error.trimmed().split('\n', Qt::SkipEmptyParts)) last = line.trimmed();
        m_view->setError(last, -1);
        if (mine.isEmpty()) return;
    }
    else
    {
        m_view->setError(QString(), -1);
    }
    if (r.script == m_doc->toPlainText()) setVarSpans(r.vars);
    for (int i = 0; i < mine.size(); ++i) mine[i]->setSourceLine(100000 + i);
    m_view->onSettingsFromScript(r.settings, false);
    m_view->setShapes(mine);
    m_view->setFieldSources(r.fields);
    if (m_firstPicture && !mine.isEmpty())
    {
        m_view->zoomTo();
        m_firstPicture = false;
    }
    QSet<int> still;
    for (int l : m_selected) if (l >= 100000 && l < 100000 + mine.size()) still.insert(l);
    m_selected = still;
    applySelection();
}

void AppWindow::onResetPicture()
{
    if (!m_hasViewport) return;
    m_script = m_originalScript;
    m_doc->setPlainText(m_script);
    m_varSpans.clear();
    if (m_resetButton) m_resetButton->hide();
    scheduleLive(0);
}

void AppWindow::setVarSpans(const QMap<libfive::Tree::Id, QRect>& rects)
{
    m_varSpans.clear();
    // (Python's columns are bytes of UTF-8, the document's are characters: a line with an accent before a number is told apart)
    QHash<int, QPair<bool, QByteArray>> lines;
    auto charColumn = [&](int line, const QString& text, int byteColumn) {
        auto found = lines.find(line);
        if (found == lines.end())
        {
            bool ascii = true;
            for (const QChar c : text) ascii = ascii && c.unicode() < 128;
            found = lines.insert(line, qMakePair(ascii, ascii ? QByteArray() : text.toUtf8()));
        }
        return found->first ? byteColumn : QString::fromUtf8(found->second.left(byteColumn)).size();
    };
    for (auto it = rects.begin(); it != rects.end(); ++it)
    {
        const QRect& r = it.value();
        if (r.height() != 0) continue;                      // (a call over several lines: its number is not written to)
        const QTextBlock block = m_doc->findBlockByNumber(r.top());
        if (!block.isValid()) continue;
        const QString text = block.text();
        // (the rectangle goes from the first character of the number to the `)` that ends the call, which is not part of the number)
        const int first = charColumn(r.top(), text, r.left());
        const int last = charColumn(r.top(), text, r.left() + r.width() - 1);
        if (first < 0 || last <= first || last >= text.size() || text[last] != QLatin1Char(')')) continue;
        VarSpan span{QTextCursor(m_doc), QTextCursor(m_doc), text.mid(first, last - first)};
        span.start.setPosition(block.position() + first);
        span.end.setPosition(block.position() + last);
        m_varSpans.insert(it.key(), span);
    }
}

void AppWindow::applyVarValues(const QMap<libfive::Tree::Id, float>& vs)
{
    QTextCursor cursor(m_doc);
    cursor.beginEditBlock();
    for (auto v = vs.begin(); v != vs.end(); ++v)
    {
        auto it = m_varSpans.find(v.key());
        if (it == m_varSpans.end()) continue;
        VarSpan& span = it.value();
        const int a = span.start.position(), b = span.end.position();
        QTextCursor look(m_doc);
        look.setPosition(a);
        look.setPosition(b, QTextCursor::KeepAnchor);
        if (b <= a || look.selectedText() != span.text || m_doc->characterAt(b) != QLatin1Char(')'))
        {
            m_varSpans.erase(it);
            continue;
        }
        cursor.setPosition(a);
        cursor.setPosition(b, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        QString str;
        str.setNum(v.value());
        cursor.insertText(str);
        span.start.setPosition(a);
        span.end.setPosition(a + str.size());
        span.text = str;
    }
    cursor.endEditBlock();
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// The automation: a developer's way of driving this window in a test run (FIELDES_AUTOMATION)

void AppWindow::registerAutomation()
{
    const QString file = qEnvironmentVariable("FIELDES_AUTOMATION");
    if (file.isEmpty()) return;
    auto a = new Automation(this, this);
    // appset <key> <value>: sets an input as a hand does
    a->add("appset", [this](const QString& args) {
        const QString key = args.section(' ', 0, 0);
        if (auto f = field(key)) f->set(QJsonValue(args.section(' ', 1))); else std::cerr << "automation: no input " << key.toStdString() << "\n";
    });
    a->add("apprun", [this](const QString&) { onRun(); });
    a->add("appstop", [this](const QString&) { onStop(); });
    a->add("appwatch", [this](const QString& args) { m_watchBox->setChecked(args.trimmed() == "on"); });
    // appwait <seconds>: until the app is not running and the interpreter is ready
    a->add("appwait", [this](const QString& args) {
        const qint64 limit = std::max(1, args.trimmed().toInt()) * 1000;
        QElapsedTimer total;
        total.start();
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, [&] { if ((m_ready && !m_running && !m_picture && !m_runAgain && !m_runAfter && !m_liveAgain && !m_liveTimer.isActive() && !m_watchDebounce.isActive()) || total.elapsed() > limit) loop.quit(); });
        poll.start(100);
        loop.exec();
        std::cerr << "automation: app idle after " << total.elapsed() / 1000.0 << " s (ready=" << m_ready << ")\n";
    });
    // appdump <file>: the form, the status, the outputs and the log, as text
    a->add("appdump", [this](const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream out(&f);
        out.setCodec("UTF-8");
        out << "title: " << windowTitle() << "\n";
        out << "kind: " << m_kind << "\n";
        out << "running: " << m_running << "  ready: " << m_ready << "  run enabled: " << m_runButton->isEnabled() << "  button: " << m_runButton->text() << "\n";
        out << "watch: " << m_watchBox->isChecked() << "\n";
        for (const auto& fl : m_fields)
        {
            const auto v = fl.get();
            out << "input " << fl.key << " [" << fl.kind << "] = " << (v.isString() ? v.toString() : v.isBool() ? (v.toBool() ? "true" : "false") : QString::number(v.toDouble(), 'g', 10));
            if (!fl.error->isHidden()) out << "   ERROR: " << fl.error->text();
            out << "\n";
        }
        out << "status: " << m_status->text() << "\n";
        for (int i = 0; i < m_outputsLayout->count(); ++i)
            if (auto row = m_outputsLayout->itemAt(i)->widget())
                for (auto l : row->findChildren<QLabel*>()) out << "output: " << l->text() << "\n";
        out << "log:\n" << m_log->toPlainText() << "\n";
    });
    // The viewport: appview <file> says what it holds (each model: its name, whether it is a point, selected, locked, has a gizmo, and where it is
    // on the screen); appclick <name> [dot], appdrag <name> <dx> <dy> [dot], appgizmodrag <kind> <axis> <dx> <dy>, appclickat <x> <y> work in the
    // viewport's own coordinates; appreset presses the reset button
    auto find = [this](const QString& name) -> Shape* {
        if (!m_view) return nullptr;
        for (Shape* s : m_view->allShapes()) if (s->appLabel() == name || QString(s->appLabel()).replace(' ', '_') == name) return s;   // (a name with a space is written with an underscore)
        return nullptr;
    };
    auto anchor = [this](Shape* s, bool dot, QPoint& at) {
        QVector3D p;
        if ((dot || !s->hasMesh()) && s->dotPosition(p)) { at = m_view->screenPoint(p); return true; }
        if (s->hasMesh() && !s->noPaint())
        {
            const auto& b = s->getMeshBounds();
            at = m_view->screenPoint(QVector3D(0.5f * (b.lower.x() + b.upper.x()), 0.5f * (b.lower.y() + b.upper.y()), 0.5f * (b.lower.z() + b.upper.z())));
            return true;
        }
        return false;
    };
    auto send = [this](QEvent::Type t, QPoint pos, Qt::MouseButton b, Qt::MouseButtons bs) {
        QMouseEvent e(t, pos, m_view->mapToGlobal(pos), b, bs, Qt::NoModifier);
        QApplication::sendEvent(m_view, &e);
    };
    a->add("appview", [this, anchor](const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream out(&f);
        out.setCodec("UTF-8");
        out << "viewport: " << (m_view ? 1 : 0) << "  shapes: " << (m_view ? m_view->shapeCount() : 0) << "  edited: " << (m_script != m_originalScript) << "\n";
        if (m_view)
            for (Shape* s : m_view->allShapes())
            {
                QPoint at;
                const bool has = anchor(s, true, at);
                out << "shape '" << s->appLabel() << "' line " << s->sourceLine() << " point " << s->isPoint() << " selected " << s->isSelected() << " locked "
                    << s->isLocked() << " gizmo " << s->hasGizmo() << " mesh " << s->hasMesh() << " at " << (has ? at.x() : -1) << "," << (has ? at.y() : -1) << "\n";
            }
        if (m_view)
            for (const QString& k : m_view->openFieldKeys()) out << "open field " << k << "\n";
        out << "script:\n" << m_script << "\n";
    });
    a->add("appfieldclick", [this](const QString& key) {
        QPoint at;
        if (!m_view || !m_view->fieldIconPoint(key.trimmed(), at)) { std::cerr << "automation: no field icon " << key.toStdString() << "\n"; return; }
        auto send = [&](QEvent::Type t, Qt::MouseButton b, Qt::MouseButtons bs) {
            QMouseEvent e(t, at, m_view->mapToGlobal(at), b, bs, Qt::NoModifier);
            QApplication::sendEvent(m_view, &e);
        };
        send(QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
    });
    a->add("appfieldarrow", [this](const QString& args) {
        const auto p = args.split(' ', Qt::SkipEmptyParts);
        QPoint from;
        if (p.size() < 4 || !m_view || !m_view->fieldGripPoint(p[0], p[1].toInt(), from)) { std::cerr << "automation: no field arrow " << args.toStdString() << "\n"; return; }
        const QPoint d(p[2].toInt(), p[3].toInt());
        auto send = [&](QEvent::Type t, QPoint pos, Qt::MouseButton b, Qt::MouseButtons bs) {
            QMouseEvent e(t, pos, m_view->mapToGlobal(pos), b, bs, Qt::NoModifier);
            QApplication::sendEvent(m_view, &e);
        };
        send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i) send(QEvent::MouseMove, from + d * i / 8, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, from + d, Qt::LeftButton, Qt::NoButton);
    });
    a->add("appfielddrag", [this](const QString& args) {
        const auto p = args.split(' ', Qt::SkipEmptyParts);
        QPoint from;
        if (p.size() < 3 || !m_view || !m_view->fieldIconPoint(p[0], from)) { std::cerr << "automation: no field icon " << args.toStdString() << "\n"; return; }
        const QPoint d(p[1].toInt(), p[2].toInt());
        auto send = [&](QEvent::Type t, QPoint pos, Qt::MouseButton b, Qt::MouseButtons bs) {
            QMouseEvent e(t, pos, m_view->mapToGlobal(pos), b, bs, Qt::NoModifier);
            QApplication::sendEvent(m_view, &e);
        };
        send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i) send(QEvent::MouseMove, from + d * i / 8, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, from + d, Qt::LeftButton, Qt::NoButton);
    });
    a->add("appclick", [this, find, anchor, send](const QString& args) {
        const QString name = args.section(' ', 0, 0);
        Shape* s = find(name);
        QPoint at;
        if (!s || !anchor(s, args.contains("dot"), at)) { std::cerr << "automation: no model " << name.toStdString() << "\n"; return; }
        send(QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
        send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    });
    a->add("appclickat", [this, send](const QString& args) {
        const auto p = args.split(' ', Qt::SkipEmptyParts);
        if (p.size() < 2 || !m_view) return;
        const QPoint at(p[0].toInt(), p[1].toInt());
        send(QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
        send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    });
    a->add("appdrag", [this, find, anchor, send](const QString& args) {
        const auto p = args.split(' ', Qt::SkipEmptyParts);
        Shape* s = p.size() >= 3 ? find(p[0]) : nullptr;
        QPoint from;
        if (!s || !anchor(s, args.contains("dot"), from)) { std::cerr << "automation: no model " << args.toStdString() << "\n"; return; }
        const QPoint d(p[1].toInt(), p[2].toInt());
        send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i) send(QEvent::MouseMove, from + d * i / 8, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, from + d, Qt::LeftButton, Qt::NoButton);
    });
    a->add("appgizmodrag", [this, send](const QString& args) {
        const auto p = args.split(' ', Qt::SkipEmptyParts);
        QPoint from;
        if (p.size() < 4 || !m_view || !m_view->gizmoGripPoint(p[0].toInt(), p[1].toInt(), from)) { std::cerr << "automation: no handle " << args.toStdString() << "\n"; return; }
        const QPoint d(p[2].toInt(), p[3].toInt());
        send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i) send(QEvent::MouseMove, from + d * i / 8, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, from + d, Qt::LeftButton, Qt::NoButton);
    });
    a->add("appreset", [this](const QString&) { onResetPicture(); });
    // widgetglobal <objectName>: where a widget is on the screen (its top left corner and size, in the pixels Qt reports, and the device pixel ratio);
    // widgetgeom <objectName>: where it is in its parent -- for a driver that moves the real mouse over a card
    a->add("widgetglobal", [this](const QString& name) {
        auto w = findChild<QWidget*>(name.trimmed());
        if (!w) { std::cerr << "automation: no widget " << name.toStdString() << "\n"; return; }
        const QPoint g = w->mapToGlobal(QPoint(0, 0));
        std::cerr << "automation: widgetglobal " << name.trimmed().toStdString() << " visible=" << w->isVisible() << " at " << g.x() << "," << g.y()
                  << " size " << w->width() << "x" << w->height() << " dpr " << w->devicePixelRatioF() << "\n";
    });
    a->add("widgetgeom", [this](const QString& name) {
        auto w = findChild<QWidget*>(name.trimmed());
        if (!w) { std::cerr << "automation: no widget " << name.toStdString() << "\n"; return; }
        std::cerr << "automation: " << name.trimmed().toStdString() << " visible=" << w->isVisible() << " at " << w->x() << "," << w->y()
                  << " size " << w->width() << "x" << w->height() << "\n";
    });
    a->add("apprightclick", [this, send](const QString& args) {
        const auto p = args.split(' ', Qt::SkipEmptyParts);
        if (p.size() < 2 || !m_view) return;
        const QPoint at(p[0].toInt(), p[1].toInt());
        QMouseEvent press(QEvent::MouseButtonPress, at, m_view->mapToGlobal(at), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(m_view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, at, m_view->mapToGlobal(at), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(m_view, &release);
        std::cerr << "automation: right click done, popup open: " << (QApplication::activePopupWidget() != nullptr) << "\n";
    });
    if (a->load(file)) QTimer::singleShot(1500, a, &Automation::start);
    else std::cerr << "AppWindow: could not read the automation file " << file.toStdString() << "\n";
}

}   // namespace FielDes
