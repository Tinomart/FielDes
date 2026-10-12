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
#include <QTextCursor>
#include <QTextStream>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "fieldes/appwindow.hpp"
#include "fieldes/automation.hpp"
#include "fieldes/i18n.hpp"
#include "fieldes/python/interpreter.hpp"
#include "fieldes/theme.hpp"
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

const char* kMuted = "#6b7280";
const char* kError = "#c0392b";
const char* kGood = "#2e7d32";

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
    auto central = new QWidget(this);
    auto outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_header = new QWidget(central);
    m_header->setObjectName("appHeader");
    m_header->setStyleSheet(QString("#appHeader { background: %1; border-bottom: 1px solid %2; }")
                                .arg(Theme::surface.name(), Theme::border.name()));
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
    m_runButton->setDefault(true);
    m_runButton->setMinimumWidth(110);
    m_runButton->setMinimumHeight(32);
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
    below->setStyleSheet(QString("QWidget#appBelow { border-top: 1px solid %1; }").arg(Theme::border.name()));
    below->setObjectName("appBelow");
    outer->addWidget(below, 1);
    setCentralWidget(central);

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
    m_manifest = doc["manifest"].toObject();
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
}

void AppWindow::onRun()
{
    if (!m_ready || m_running || !m_open) return;
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
    for (auto s : r.shapes) s->deleteLater();
    const double seconds = m_clock.elapsed() / 1000.0;
    const bool terminated = m_stopped && !r.okay;
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
        QObject::connect(&poll, &QTimer::timeout, [&] { if ((m_ready && !m_running && !m_runAgain && !m_watchDebounce.isActive()) || total.elapsed() > limit) loop.quit(); });
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
    if (a->load(file)) QTimer::singleShot(1500, a, &Automation::start);
    else std::cerr << "AppWindow: could not read the automation file " << file.toStdString() << "\n";
}

}   // namespace FielDes
