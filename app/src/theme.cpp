/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <QApplication>
#include <QHash>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

#include <vector>

#include "fieldes/i18n.hpp"
#include "fieldes/theme.hpp"

namespace FielDes {

// (the colours of the light set until load() says otherwise)
QColor Theme::chrome("#ebe8de");
QColor Theme::surface("#ffffff");
QColor Theme::paper("#f8f6ef");
QColor Theme::gutter("#eeebe2");
QColor Theme::border("#d3cfc0");
QColor Theme::text("#2e3b43");
QColor Theme::muted("#6b7b83");
QColor Theme::accent("#268bd2");

namespace {

struct Entry
{
    const char* key;
    const char* light;
    const char* dark;
    QColor* now;
};

// The dark set: the window a dark teal a shade below the bars (#164c5e), the editor in Solarized's darkest base and its next one for the numbers
const std::vector<Entry>& table()
{
    static const std::vector<Entry> entries = {
        {"chrome",  "#ebe8de", "#0f3a47", &Theme::chrome},
        {"surface", "#ffffff", "#164c5e", &Theme::surface},
        {"paper",   "#f8f6ef", "#002b36", &Theme::paper},
        {"gutter",  "#eeebe2", "#073642", &Theme::gutter},
        {"border",  "#d3cfc0", "#2c5b69", &Theme::border},
        {"text",    "#2e3b43", "#eee8d5", &Theme::text},
        {"muted",   "#6b7b83", "#93a1a1", &Theme::muted},
        {"accent",  "#268bd2", "#268bd2", &Theme::accent},
    };
    return entries;
}

Theme::Mode g_mode = Theme::Mode::Light;
QHash<QString, QColor> g_custom[2];             // (what was chosen by hand, for the light set and for the dark one)

int slot(Theme::Mode m) { return m == Theme::Mode::Dark ? 1 : 0; }

QString settingsKey(Theme::Mode m, const QString& key)
{
    return QString("theme/%1/%2").arg(m == Theme::Mode::Dark ? "dark" : "light", key);
}

// The eight colours from the set that is on and what was chosen in it
void refresh()
{
    for (const Entry& e : table())
        *e.now = Theme::color(QString::fromLatin1(e.key));
}

}   // namespace

Theme::Mode Theme::mode() { return g_mode; }
bool Theme::dark() { return g_mode == Mode::Dark; }

QColor Theme::mix(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

// (each derived colour is what the light set had as a literal, to within a shade, and the same rule gives the dark one)
QColor Theme::field() { return dark() ? border.lighter(125) : border.darker(106); }
QColor Theme::pressed() { return mix(surface, accent, 0.12); }
QColor Theme::selection() { return mix(paper, accent, dark() ? 0.40 : 0.22); }
QColor Theme::aux() { return dark() ? paper.lighter(120) : paper.darker(103); }
QColor Theme::handle() { return dark() ? mix(paper, text, 0.25) : border.darker(103); }
QColor Theme::handleHover() { return dark() ? mix(paper, text, 0.40) : border.darker(120); }
QColor Theme::tabLine() { return mix(muted, paper, 0.4); }
QColor Theme::disabled() { return mix(text, chrome, 0.62); }
QString Theme::hoverShade() { return dark() ? "rgba(255, 255, 255, 30)" : "rgba(0, 0, 0, 28)"; }

QList<Theme::Role> Theme::roles()
{
    // (the words are written out where the translations are looked for in the source)
    return {
        {"chrome", T("Window")},
        {"surface", T("Bars and buttons")},
        {"paper", T("Editor")},
        {"gutter", T("Line numbers")},
        {"border", T("Lines")},
        {"text", T("Text")},
        {"muted", T("Quiet text")},
        {"accent", T("Selection and focus")},
    };
}

QColor Theme::defaultColor(const QString& key, Mode m)
{
    for (const Entry& e : table())
        if (key == QLatin1String(e.key)) return QColor(m == Mode::Dark ? e.dark : e.light);
    return QColor();
}

QColor Theme::color(const QString& key)
{
    const auto& chosen = g_custom[slot(g_mode)];
    return chosen.contains(key) ? chosen.value(key) : defaultColor(key, g_mode);
}

bool Theme::isCustom(const QString& key) { return g_custom[slot(g_mode)].contains(key); }
bool Theme::anyCustom() { return !g_custom[slot(g_mode)].isEmpty(); }

void Theme::load()
{
    QSettings s("FielDes", "FielDes");
    g_mode = s.value("theme/mode", "light").toString() == "dark" ? Mode::Dark : Mode::Light;
    for (const Mode m : {Mode::Light, Mode::Dark})
    {
        g_custom[slot(m)].clear();
        for (const Entry& e : table())
        {
            const QColor c(s.value(settingsKey(m, QString::fromLatin1(e.key))).toString());
            if (c.isValid() && c != defaultColor(QString::fromLatin1(e.key), m)) g_custom[slot(m)].insert(QString::fromLatin1(e.key), c);
        }
    }
    refresh();
}

void Theme::setMode(Mode m, bool save)
{
    g_mode = m;
    if (save) QSettings("FielDes", "FielDes").setValue("theme/mode", m == Mode::Dark ? "dark" : "light");
    refresh();
}

void Theme::setColor(const QString& key, const QColor& colour, bool save)
{
    if (!colour.isValid() || defaultColor(key, g_mode).isValid() == false) return;
    if (colour == defaultColor(key, g_mode))
    {
        resetColor(key, save);
        return;
    }
    g_custom[slot(g_mode)].insert(key, colour);
    if (save) QSettings("FielDes", "FielDes").setValue(settingsKey(g_mode, key), colour.name());
    refresh();
}

void Theme::resetColor(const QString& key, bool save)
{
    g_custom[slot(g_mode)].remove(key);
    if (save) QSettings("FielDes", "FielDes").remove(settingsKey(g_mode, key));
    refresh();
}

void Theme::resetColors(bool save)
{
    g_custom[slot(g_mode)].clear();
    if (save)
    {
        QSettings s("FielDes", "FielDes");
        for (const Entry& e : table()) s.remove(settingsKey(g_mode, QString::fromLatin1(e.key)));
    }
    refresh();
}

void Theme::apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, chrome);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, paper);
    p.setColor(QPalette::AlternateBase, gutter);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, surface);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor("#ffffff"));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    p.setColor(QPalette::ToolTipBase, QColor("#164c5e"));
    p.setColor(QPalette::ToolTipText, QColor("#eee8d5"));
    p.setColor(QPalette::Mid, muted);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled());
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled());
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled());
    app.setPalette(p);

    // (the menus, the menu bar and the status bar are the dark teal in both sets)
    QPalette bars = p;
    bars.setColor(QPalette::Window, QColor("#164c5e"));
    bars.setColor(QPalette::Base, QColor("#164c5e"));
    bars.setColor(QPalette::WindowText, QColor("#eee8d5"));
    bars.setColor(QPalette::Text, QColor("#eee8d5"));
    bars.setColor(QPalette::ButtonText, QColor("#eee8d5"));
    bars.setColor(QPalette::Disabled, QPalette::Text, QColor("#586e75"));
    bars.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#586e75"));
    bars.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#586e75"));
    app.setPalette(bars, "QMenu");
    app.setPalette(bars, "QMenuBar");
    app.setPalette(bars, "QStatusBar");

    // (the cards floating in the viewport set their own dark styles; these are the rest)
    app.setStyleSheet(QString(
        "QMenuBar { background: #164c5e; color: #eee8d5; border: none; border-bottom: 1px solid rgba(147, 161, 161, 70);"
        "  padding: 1px 8px; font-size: 10pt; }"
        "QMenuBar::item { padding: 5px 12px; margin: 1px 1px; border-radius: 6px; background: transparent; }"
        "QMenuBar::item:selected { background: rgba(147, 161, 161, 48); }"
        "QMenuBar::item:pressed { background: rgba(38, 139, 210, 210); color: white; }"
        "QWidget#TopIcons { background: transparent; }"
        "QToolButton#TopButton { border: none; border-radius: 6px; background: transparent; }"
        "QToolButton#TopButton:hover { background: rgba(147, 161, 161, 48); }"
        "QToolButton#TopButton:pressed { background: rgba(38, 139, 210, 210); }"
        "QToolButton#TopArrow { border: none; border-radius: 5px; background: transparent; }"
        "QToolButton#TopArrow:hover { background: rgba(147, 161, 161, 48); }"
        "QToolButton#TopArrow:pressed { background: rgba(38, 139, 210, 210); }"
        "QFrame#TopDivider { color: rgba(147, 161, 161, 80); }"
        "QMenu { background: #164c5e; color: #eee8d5; border: 1px solid rgba(147, 161, 161, 100); padding: 4px 0px;"
        "  font-size: 10pt; }"
        "QMenu::item { padding: 6px 30px 6px 26px; margin: 0px 4px; border-radius: 4px; }"
        "QMenu::item:selected { background: rgba(38, 139, 210, 210); color: white; }"
        "QMenu::item:disabled { color: #586e75; }"
        "QMenu::separator { height: 1px; background: rgba(147, 161, 161, 70); margin: 4px 10px; }"
        "QStatusBar { background: #164c5e; color: #93a1a1; border-top: 1px solid rgba(147, 161, 161, 70); }"
        "QStatusBar::item { border: none; }"
        "QSplitter::handle { background: %2; }"
        "QSplitter::handle:horizontal { width: 1px; }"
        "QSplitter::handle:vertical { height: 1px; }"
        "QToolTip { background-color: #164c5e; color: #eee8d5; border: 1px solid #586e75; padding: 4px 7px; }"
        "QDialog, QMessageBox { background: %5; }"
        "QPushButton { background: %1; color: %3; border: 1px solid %6; border-radius: 5px; padding: 5px 16px; }"
        "QPushButton:hover { border-color: %4; }"
        "QPushButton:pressed { background: %7; }"
        "QPushButton:default { border-color: %4; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: %1; color: %3; border: 1px solid %6;"
        "  border-radius: 4px; padding: 3px 6px; selection-background-color: %4; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %4; }"
        // (the list that opens from a drop-down in a light window: it took the dark palette of the menus -- #164c5e -- under the dark text of the window, and five of the six kinds of the primitives manager could not be read.
        // Here it is the window's own scheme: the colour of the fields, a border, the text, the accent on the item under the cursor.  The drop-downs of the dark cards in the viewport are not in a dialog and keep their own)
        "QDialog QComboBox QAbstractItemView, QDialog QComboBoxPrivateContainer { background: %1; color: %3; border: 1px solid %6;"
        "  selection-background-color: %4; selection-color: #ffffff; outline: 0; }"
        "QPlainTextEdit QScrollBar:vertical, QTextBrowser QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }"
        "QPlainTextEdit QScrollBar::handle:vertical, QTextBrowser QScrollBar::handle:vertical {"
        "  background: %8; border-radius: 4px; min-height: 28px; margin: 2px; }"
        "QPlainTextEdit QScrollBar::handle:vertical:hover, QTextBrowser QScrollBar::handle:vertical:hover { background: %9; }"
        "QPlainTextEdit QScrollBar::add-line:vertical, QPlainTextEdit QScrollBar::sub-line:vertical,"
        "QTextBrowser QScrollBar::add-line:vertical, QTextBrowser QScrollBar::sub-line:vertical { height: 0; }"
        "QPlainTextEdit QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }"
        "QPlainTextEdit QScrollBar::handle:horizontal { background: %8; border-radius: 4px; min-width: 28px; margin: 2px; }"
        "QPlainTextEdit QScrollBar::add-line:horizontal, QPlainTextEdit QScrollBar::sub-line:horizontal { width: 0; }")
        .arg(surface.name(), border.name(), text.name(), accent.name(), chrome.name(), field().name(), pressed().name(), handle().name(), handleHover().name()));

    // Every widget is made again by the new palette and style sheet: a widget made before a change of set (the Fix All button of the output, a button of a dialog that is open) is not
    // always re-polished by the style sheet alone, and kept the colours of the set before
    for (QWidget* w : app.allWidgets())
    {
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    }
}

}   // namespace FielDes
