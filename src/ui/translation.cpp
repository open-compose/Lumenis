// The interface's language (ROADMAP 10.4; docs/spec/localisation.md): the languages built in, the one chosen, its
// translators and the pseudo-locale. translation.h says what each part is for.
#include "lumenis/ui/translation.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>

#include <cmath>
#include <memory>
#include <vector>

namespace lumenis::ui {

namespace {

constexpr const char* kPseudo = "pseudo";
constexpr const char* kSettingsKey = "interface/language";

// Our compiled translations, as resources (src/ui/CMakeLists.txt, qt_add_lrelease).
QString ourFile(const QString& code) {
    return QStringLiteral(":/lumenis/translations/cutthulhu_%1.qm").arg(code);
}

// Each Latin letter with an accent, so that untranslated text (plain letters) stands out.
QChar accented(QChar c) {
    static const QString plain = QStringLiteral("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    static const QString marked = QStringLiteral("áƀçđéƒĝĥîĵķĺɱñöþǫŕšţûṽŵẋýžÁƁÇĐÉƑĜĤÎĴĶĹṀÑÖÞǪŔŠŢÛṼŴẊÝŽ");
    const qsizetype at = plain.indexOf(c);
    return at < 0 ? c : marked.at(at);
}

// The pseudo-locale's translator: every text through pseudoText(), so it needs no file.
class PseudoTranslator : public QTranslator {
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override { return false; }
    QString translate(const char*, const char* source, const char* = nullptr, int = -1) const override {
        return source ? pseudoText(QString::fromUtf8(source)) : QString();
    }
};

// The translators installed, kept for the application's life.
std::vector<std::unique_ptr<QTranslator>>& installed() {
    static std::vector<std::unique_ptr<QTranslator>> list;
    return list;
}

QString& installedCode() {
    static QString code = QStringLiteral("en");
    return code;
}

} // namespace

QString pseudoText(const QString& source) {
    if (source.isEmpty()) return source;
    // Kept as they are: placeholders (%1, %L1, %n), HTML tags and entities, and the mnemonic's marker.
    static const QRegularExpression kept(QStringLiteral(R"(%L?\d+|%n|<[^>]*>|&[a-zA-Z]+;|&#?\w+;)"));
    QString out;
    out.reserve(source.size() * 2);
    qsizetype letters = 0;
    qsizetype at = 0;
    for (QRegularExpressionMatchIterator it = kept.globalMatch(source); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        for (qsizetype i = at; i < m.capturedStart(); ++i) {
            out += accented(source.at(i));
            if (source.at(i).isLetter()) ++letters;
        }
        out += m.captured();
        at = m.capturedEnd();
    }
    for (qsizetype i = at; i < source.size(); ++i) {
        out += accented(source.at(i));
        if (source.at(i).isLetter()) ++letters;
    }
    // About 40% longer: German and French run that much longer than English, the usual allowance.
    const auto padding = static_cast<qsizetype>(std::ceil(static_cast<double>(letters) * 0.4));
    return QLatin1Char('[') + out + (padding > 0 ? QLatin1Char(' ') + QString(padding, QLatin1Char('~')) : QString()) +
           QLatin1Char(']');
}

QList<Language> availableLanguages() {
    QList<Language> out{{QStringLiteral("en"), QStringLiteral("English")}};
    QFile file(QStringLiteral(":/lumenis/translations/languages.json"));
    if (file.open(QIODevice::ReadOnly)) {
        for (const QJsonValue& v : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("languages")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString code = o.value(QStringLiteral("code")).toString();
            // Listed but not compiled in (a build without Qt's Linguist tools): not offered.
            if (code.isEmpty() || code == QLatin1String("en") || !QFile::exists(ourFile(code))) continue;
            out << Language{code, o.value(QStringLiteral("name")).toString()};
        }
    }
    out << Language{QString::fromLatin1(kPseudo), QStringLiteral("Pseudo-locale (for testing)")};
    return out;
}

QString startupLanguage(const QString& requested, bool useSaved) {
    QString code = requested;
    if (code.isEmpty()) code = qEnvironmentVariable("LUMENIS_LANGUAGE");
    if (code.isEmpty() && useSaved) code = QSettings().value(QString::fromLatin1(kSettingsKey)).toString();
    for (const Language& l : availableLanguages()) {
        if (l.code == code) return code;
    }
    return QStringLiteral("en");
}

void saveLanguage(const QString& code) {
    QSettings settings;
    const QString saved = settings.value(QString::fromLatin1(kSettingsKey)).toString();
    // Written only when it changes, and never the default over nothing: reading the Interface setting at every start
    // (and in every test) must not write to the user's own settings.
    if (code == saved || (saved.isEmpty() && code == QLatin1String("en"))) return;
    settings.setValue(QString::fromLatin1(kSettingsKey), code);
}

bool installLanguage(QApplication& app, const QString& code) {
    if (code.isEmpty() || code == QLatin1String("en")) return true;
    if (code == QLatin1String(kPseudo)) {
        auto pseudo = std::make_unique<PseudoTranslator>();
        app.installTranslator(pseudo.get());
        installed().push_back(std::move(pseudo));
        installedCode() = code;
        return true;
    }
    auto ours = std::make_unique<QTranslator>();
    if (!ours->load(ourFile(code))) return false;
    // Qt's own (the standard buttons and dialogs), where the Qt installation has them.
    auto qt = std::make_unique<QTranslator>();
    if (qt->load(QLocale(code), QStringLiteral("qtbase"), QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(qt.get());
        installed().push_back(std::move(qt));
    }
    app.installTranslator(ours.get());
    installed().push_back(std::move(ours));
    installedCode() = code;
    // Numbers, dates and the rest in the language's way too.
    QLocale::setDefault(QLocale(code));
    return true;
}

QString installedLanguage() {
    return installedCode();
}

} // namespace lumenis::ui
