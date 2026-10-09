/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

#include "fieldes/i18n.hpp"
#include "fieldes/findbar.hpp"
#include "fieldes/script.hpp"
#include "fieldes/color.hpp"

namespace FielDes {

FindBar::FindBar(Script* script, QWidget* parent)
    : QWidget(parent), m_script(script),
      m_find(new QLineEdit), m_replace(new QLineEdit),
      m_replaceRow(new QWidget),
      m_case(new QToolButton), m_word(new QToolButton), m_regex(new QToolButton),
      m_count(new QLabel)
{
    m_find->setPlaceholderText(T("Find"));
    m_replace->setPlaceholderText(T("Replace"));
    m_find->setClearButtonEnabled(true);
    m_find->installEventFilter(this);
    m_replace->installEventFilter(this);

    auto option = [](QToolButton* b, QString text, QString tip) {
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setAutoRaise(true);
    };
    option(m_case, "Aa", T("Match case (Alt+C)"));
    option(m_word, "W", T("Whole words (Alt+W)"));
    option(m_regex, ".*", T("Regular expression (Alt+R)"));

    auto button = [this](QString text, QString tip, auto slot) {
        auto b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, slot);
        return b;
    };

    auto findRow = new QHBoxLayout;
    findRow->setContentsMargins(0, 0, 0, 0);
    findRow->setSpacing(2);
    findRow->addWidget(m_find, 1);
    findRow->addWidget(m_case);
    findRow->addWidget(m_word);
    findRow->addWidget(m_regex);
    findRow->addWidget(m_count);
    findRow->addWidget(button("↑", T("Previous match (Shift+Enter / Shift+F3)"),
                              &FindBar::findPrevious));
    findRow->addWidget(button("↓", T("Next match (Enter / F3)"), &FindBar::findNext));
    findRow->addWidget(button(T("All"), T("Select all matches for simultaneous editing (Alt+Enter)"),
                              &FindBar::selectAllMatches));
    findRow->addWidget(button("✕", T("Close (Esc)"), &FindBar::closeBar));

    auto replaceRow = new QHBoxLayout(m_replaceRow);
    replaceRow->setContentsMargins(0, 0, 0, 0);
    replaceRow->setSpacing(2);
    replaceRow->addWidget(m_replace, 1);
    replaceRow->addWidget(button(T("Replace"), T("Replace this match (Enter in replace field)"),
                                 &FindBar::replaceOne));
    replaceRow->addWidget(button(T("Replace all"), T("Replace every match (Ctrl+Alt+Enter)"),
                                 &FindBar::replaceAll));

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);
    layout->addLayout(findRow);
    layout->addWidget(m_replaceRow);

    m_refresh.setSingleShot(true);
    m_refresh.setInterval(120);
    connect(&m_refresh, &QTimer::timeout, this, &FindBar::updateMatches);
    connect(m_find, &QLineEdit::textChanged, this, [this]{
        updateMatches();
        // Incremental search: move to the first match at/after the cursor
        if (!m_matches.isEmpty())
        {
            const int pos = m_script->textCursor().selectionStart();
            int i = 0;
            while (i < m_matches.size() && m_matches[i].selectionStart() < pos) ++i;
            jumpTo(i % m_matches.size());
        }
    });
    for (auto b : {m_case, m_word, m_regex})
    {
        connect(b, &QToolButton::toggled, this, &FindBar::updateMatches);
    }
    connect(m_script->document(), &QTextDocument::contentsChanged,
            this, [this]{ if (isVisible()) m_refresh.start(); });

    hide();
}

void FindBar::showFind()    { open(false); }
void FindBar::showReplace() { open(true); }

void FindBar::open(bool replace)
{
    m_replaceRow->setVisible(replace);
    show();

    // Seed the search with the selection (if it's a single line)
    const QString sel = m_script->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
    {
        m_find->setText(m_regex->isChecked() ? QRegularExpression::escape(sel) : sel);
    }
    m_find->setFocus();
    m_find->selectAll();
    updateMatches();
}

void FindBar::closeBar()
{
    hide();
    m_matches.clear();
    m_script->setSelections(Script::SEL_FIND, {});
    m_script->setFocus();
}

QRegularExpression FindBar::pattern(bool* valid) const
{
    QString p = m_regex->isChecked() ? m_find->text()
                                     : QRegularExpression::escape(m_find->text());
    if (m_word->isChecked())
    {
        p = "\\b" + p + "\\b";
    }
    QRegularExpression re(p, m_case->isChecked()
            ? QRegularExpression::NoPatternOption
            : QRegularExpression::CaseInsensitiveOption);
    if (valid) *valid = re.isValid() && !m_find->text().isEmpty();
    return re;
}

void FindBar::updateMatches()
{
    m_matches.clear();
    bool valid = false;
    const QRegularExpression re = pattern(&valid);
    if (valid)
    {
        const QString text = m_script->document()->toPlainText();
        auto it = re.globalMatch(text);
        while (it.hasNext() && m_matches.size() < 10000)
        {
            const auto m = it.next();
            if (m.capturedLength() == 0) continue;
            QTextCursor c(m_script->document());
            c.setPosition(m.capturedStart());
            c.setPosition(m.capturedEnd(), QTextCursor::KeepAnchor);
            m_matches.append(c);
        }
    }

    QList<QTextEdit::ExtraSelection> sels;
    QTextCharFormat fmt;
    fmt.setBackground(QColor(Color::yellow.red(), Color::yellow.green(),
                             Color::yellow.blue(), 80));
    for (const auto& c : m_matches)
    {
        QTextEdit::ExtraSelection s;
        s.cursor = c;
        s.format = fmt;
        sels.append(s);
    }
    m_script->setSelections(Script::SEL_FIND, sels);

    const bool bad = !m_find->text().isEmpty() && !valid;
    m_find->setStyleSheet(bad || (valid && m_matches.isEmpty())
        ? "QLineEdit { background: #f8d7d0; }" : QString());
    const int cur = currentIndex();
    m_count->setText(m_matches.isEmpty() ? (m_find->text().isEmpty() ? QString() : T("No results"))
                   : cur >= 0 ? T("%1 of %2").arg(cur + 1).arg(m_matches.size())
                              : T("%1 found").arg(m_matches.size()));
}

int FindBar::currentIndex() const
{
    const QTextCursor c = m_script->textCursor();
    for (int i=0; i < m_matches.size(); ++i)
    {
        if (m_matches[i].selectionStart() == c.selectionStart() &&
            m_matches[i].selectionEnd() == c.selectionEnd())
        {
            return i;
        }
    }
    return -1;
}

void FindBar::jumpTo(int index)
{
    if (index < 0 || index >= m_matches.size())
    {
        return;
    }
    const QTextCursor& c = m_matches[index];
    m_script->revealBlock(c.block());
    m_script->setTextCursor(c);
    m_script->centerCursor();
    m_count->setText(T("%1 of %2").arg(index + 1).arg(m_matches.size()));
}

void FindBar::findNext()
{
    if (!isVisible()) { showFind(); return; }
    updateMatches();
    if (m_matches.isEmpty()) return;
    const int pos = m_script->textCursor().selectionEnd();
    for (int i=0; i < m_matches.size(); ++i)
    {
        if (m_matches[i].selectionStart() >= pos && currentIndex() != i)
        {
            jumpTo(i);
            return;
        }
    }
    jumpTo(0);   // wrap
}

void FindBar::findPrevious()
{
    if (!isVisible()) { showFind(); return; }
    updateMatches();
    if (m_matches.isEmpty()) return;
    const int pos = m_script->textCursor().selectionStart();
    for (int i=m_matches.size() - 1; i >= 0; --i)
    {
        if (m_matches[i].selectionEnd() <= pos && currentIndex() != i)
        {
            jumpTo(i);
            return;
        }
    }
    jumpTo(m_matches.size() - 1);   // wrap
}

QString FindBar::expandReplacement(const QString& matched) const
{
    if (!m_regex->isChecked())
    {
        return m_replace->text();
    }
    // Expand \1..\9 / $1..$9 from the regex match
    bool valid;
    const auto re = pattern(&valid);
    const auto m = re.match(matched);
    const QString r = m_replace->text();
    QString out;
    for (int i=0; i < r.size(); ++i)
    {
        if ((r[i] == '\\' || r[i] == '$') && i + 1 < r.size() && r[i + 1].isDigit())
        {
            out += m.captured(r[i + 1].digitValue());
            ++i;
        }
        else if (r[i] == '\\' && i + 1 < r.size() && r[i + 1] == 'n')
        {
            out += '\n';
            ++i;
        }
        else
        {
            out += r[i];
        }
    }
    return out;
}

void FindBar::replaceOne()
{
    updateMatches();
    const int cur = currentIndex();
    if (cur < 0)
    {
        findNext();
        return;
    }
    QTextCursor c = m_matches[cur];
    c.insertText(expandReplacement(c.selectedText()));
    m_script->setTextCursor(c);
    updateMatches();
    findNext();
}

void FindBar::replaceAll()
{
    updateMatches();
    if (m_matches.isEmpty()) return;
    QTextCursor edit(m_script->document());
    edit.beginEditBlock();
    const int n = m_matches.size();
    for (int i=n - 1; i >= 0; --i)
    {
        QTextCursor c = m_matches[i];
        c.insertText(expandReplacement(c.selectedText()));
    }
    edit.endEditBlock();
    updateMatches();
    m_count->setText(T("Replaced %1").arg(n));
}

void FindBar::selectAllMatches()
{
    updateMatches();
    if (m_matches.isEmpty()) return;
    m_script->setMultiCursors(m_matches);
    m_script->setFocus();
}

void FindBar::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape)
    {
        closeBar();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool FindBar::eventFilter(QObject* obj, QEvent* e)
{
    // Escape closes the bar rather than triggering the window shortcut
    if (e->type() == QEvent::ShortcutOverride &&
        static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape)
    {
        e->accept();
        return true;
    }
    if (e->type() == QEvent::KeyPress)
    {
        auto k = static_cast<QKeyEvent*>(e);
        const bool enter = k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter;
        if (k->key() == Qt::Key_Escape)
        {
            closeBar();
            return true;
        }
        if (k->modifiers() & Qt::AltModifier)
        {
            switch (k->key())
            {
                case Qt::Key_C: m_case->toggle(); return true;
                case Qt::Key_W: m_word->toggle(); return true;
                case Qt::Key_R: m_regex->toggle(); return true;
                default: break;
            }
        }
        if (enter && (k->modifiers() & Qt::ControlModifier) &&
            (k->modifiers() & Qt::AltModifier))
        {
            replaceAll();
            return true;
        }
        if (enter && (k->modifiers() & Qt::AltModifier))
        {
            selectAllMatches();
            return true;
        }
        if (enter && obj == m_replace)
        {
            replaceOne();
            return true;
        }
        if (enter)
        {
            if (k->modifiers() & Qt::ShiftModifier) findPrevious(); else findNext();
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}

}   // namespace FielDes
