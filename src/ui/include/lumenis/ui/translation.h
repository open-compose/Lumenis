#pragma once

// The interface's language (ROADMAP 10.4; docs/spec/localisation.md). Our texts are in English in the code (tr()) and in
// the JSON resources (commands, menus, settings, effects); each language is a Qt Linguist file, translations/
// cutthulhu_<code>.ts, compiled into the application and listed in translations/languages.json. The language is chosen
// in the Interface setting and takes effect when Cutthulhu starts. "pseudo" is a made-up language for testing: every
// text longer and accented ("[Ŝéţţîñĝš ~~~]"), so that text the code never translates, and controls too small for a
// longer text, show at a glance.

#include <QCoreApplication>
#include <QList>
#include <QString>

class QApplication;

namespace lumenis::ui {

struct Language {
    QString code; // "en", "de", ... ("pseudo" for the pseudo-locale)
    QString name; // in its own language: "Deutsch"
};

// English, the languages built in (those of languages.json whose translations were compiled in), then the
// pseudo-locale.
QList<Language> availableLanguages();

// The language to start in: `requested` when it is given (the --language option), else LUMENIS_LANGUAGE, else the one
// saved with saveLanguage() when `useSaved`, else English. An unknown code is English.
QString startupLanguage(const QString& requested, bool useSaved);
// Remembered for the next start: the Interface setting is a User setting, and the language is needed before a user is
// chosen. Writes only a change (English over nothing saved is no change).
void saveLanguage(const QString& code);

// Installs the language's translators on the application (ours, and Qt's own for its standard dialogs and buttons);
// English installs none. Call before any window is made. Returns false (and stays in English) when the language's
// translations are not built in.
bool installLanguage(QApplication& app, const QString& code);
// The language installed, "en" if none.
QString installedLanguage();

// A text from the JSON resources in the language installed: `context` is the resource ("commands", "menus",
// "settings", "commandPalette"), as scripts/update-translations.py extracts them for Qt Linguist. Effect names,
// categories, parameters and their choices go through model::shownName, in the context "effects".
inline QString translatedResource(const char* context, const QString& text) {
    return text.isEmpty() ? text : QCoreApplication::translate(context, text.toUtf8().constData());
}

// The pseudo-locale's text for `source` (also used by its translator): letters accented, longer by about 40%, in
// brackets; placeholders (%1, %n, %L1), mnemonics (&), HTML tags and entities are kept as they are.
QString pseudoText(const QString& source);

} // namespace lumenis::ui
