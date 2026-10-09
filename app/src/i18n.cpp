/*
FielDes: field-driven design

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "fieldes/i18n.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTranslator>
#include <cstdio>
#include <cstdlib>

namespace FielDes {
namespace i18n {

namespace {

QHash<QString, QString>& catalog()
{
    static QHash<QString, QString> table;
    return table;
}

// The words Qt itself shows -- the buttons of a question ("Yes", "Cancel"), the menu of a text field ("Copy", "Paste") -- are asked for through
// the same catalog, under "qt:<context>|<text>" (Qt's translation files are not shipped)
class QtWords : public QTranslator
{
public:
    QString translate(const char* context, const char* source, const char* disambiguation = nullptr, int n = -1) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const auto it = catalog().constFind(QString("qt:%1|%2").arg(context, source));
        return it == catalog().constEnd() ? QString() : it.value();
    }
    bool isEmpty() const override { return false; }
};

QString& language()
{
    static QString code = "en";
    return code;
}

}   // namespace

const QList<Language>& languages()
{
    static const QList<Language> list = {
        {"en", "English"},   {"es", QString::fromUtf8("Espa\xc3\xb1ol")},   {"fr", QString::fromUtf8("Fran\xc3\xa7" "ais")},
        {"de", "Deutsch"},   {"ja", QString::fromUtf8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e")},
        {"zh", QString::fromUtf8("\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87")},
        {"ru", QString::fromUtf8("\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9")},
    };
    return list;
}

QString folder()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("translations");
}

QString systemLanguage()
{
    // (the first of the computer's preferred languages that has a translation: a Brazilian Portuguese computer asking for pt, es would get es)
    for (const QString& name : QLocale::system().uiLanguages())
    {
        const QString code = name.split(QRegularExpression("[-_]")).first().toLower();
        for (const auto& l : languages())
            if (l.code == code) return code;
    }
    return "en";
}

QString chosen()
{
    // (FIELDES_LANGUAGE is a language forced from outside, for a test or a screenshot.  The program never sets it itself: what it tells its
    // Python side goes in FIELDES_UI_LANGUAGE -- or a copy of the program that it restarts, which inherits its environment, would find
    // the old language here and ignore the new choice)
    const QString env = qEnvironmentVariable("FIELDES_LANGUAGE");
    if (!env.isEmpty()) return env;
    const QString value = QSettings("FielDes", "FielDes").value("language", "system").toString();
    return value.isEmpty() ? QString("system") : value;
}

void setChosen(const QString& code)
{
    QSettings("FielDes", "FielDes").setValue("language", code);
}

QString current()
{
    return language();
}

void load()
{
    QString code = chosen();
    if (code == "system") code = systemLanguage();
    bool known = false;
    for (const auto& l : languages()) known = known || l.code == code;
    if (!known) code = "en";
    language() = code;
    catalog().clear();
    // (the Python side asks for the same language)
    qputenv("FIELDES_UI_LANGUAGE", code.toUtf8());
    if (code == "en") return;
    QFile f(QDir(folder()).filePath(code + ".json"));
    if (!f.open(QIODevice::ReadOnly))
    {
        std::fprintf(stderr, "[i18n] no translations of '%s' in %s\n", code.toUtf8().constData(), folder().toUtf8().constData());
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
    {
        const QString v = it.value().toString();
        if (!v.isEmpty()) catalog().insert(it.key(), v);
    }
    static QtWords* qtWords = nullptr;
    if (!qtWords)
    {
        qtWords = new QtWords;
        QCoreApplication::installTranslator(qtWords);
    }
}

}   // namespace i18n

QString T(const QString& english)
{
    auto& table = i18n::catalog();
    const auto it = table.constFind(english);
    if (it != table.constEnd()) return it.value();
    // (a text with no translation reads in English; FIELDES_I18N_LOG names them, once each, to be translated)
    if (i18n::language() != "en" && !english.isEmpty() && qEnvironmentVariableIsSet("FIELDES_I18N_LOG"))
    {
        static QMutex lock;
        static QSet<QString> told;
        QMutexLocker guard(&lock);
        if (!told.contains(english))
        {
            told.insert(english);
            std::fprintf(stderr, "[i18n] untranslated: %s\n", english.toUtf8().constData());
        }
    }
    return english;
}

QString T(const char* english)
{
    return T(QString::fromUtf8(english));
}

}   // namespace FielDes
