// The interface's language (ROADMAP 10.4; docs/spec/localisation.md): the languages built in, the pseudo-locale's text,
// and that the JSON resources' texts (commands, menus, settings, effects) reach the screen through the translator
// installed, while what they identify (command ids, setting keys, stored effect names) stays the same.
#include "lumenis/model/shown_names.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/translation.h"

#include <QAction>
#include <QApplication>
#include <QTest>
#include <QTranslator>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// A translator with one text per context, as a .qm would give.
class OneText : public QTranslator {
public:
    OneText(QString context, QString source, QString translation)
        : context_(std::move(context)), source_(std::move(source)), translation_(std::move(translation)) {}
    bool isEmpty() const override { return false; }
    QString translate(const char* context, const char* source, const char* = nullptr, int = -1) const override {
        return QString::fromUtf8(context) == context_ && QString::fromUtf8(source) == source_ ? translation_ : QString();
    }

private:
    QString context_, source_, translation_;
};

} // namespace

class TestTranslation : public QObject {
    Q_OBJECT

private slots:
    void languagesBuiltIn() {
        const QList<Language> languages = availableLanguages();
        QVERIFY(languages.size() >= 2);
        QCOMPARE(languages.first().code, QStringLiteral("en"));
        QCOMPARE(languages.last().code, QStringLiteral("pseudo"));
        // The languages of translations/languages.json, when the build had Qt's Linguist tools.
        QStringList codes;
        for (const Language& l : languages) codes << l.code;
#ifdef LUMENIS_HAVE_TRANSLATIONS
        for (const char* code : {"de", "fr", "it", "ga"}) QVERIFY2(codes.contains(QLatin1String(code)), code);
#endif
        // Unknown languages start in English.
        QCOMPARE(startupLanguage(QStringLiteral("xx"), false), QStringLiteral("en"));
        QCOMPARE(startupLanguage(QStringLiteral("pseudo"), false), QStringLiteral("pseudo"));
        // The Interface setting offers them.
        const SettingKind* ui = SettingsSchema::builtIn().find(QStringLiteral("Interface"));
        QVERIFY(ui);
        const SettingOption* option = nullptr;
        for (const SettingTab& t : ui->tabs) {
            for (const SettingOption& o : t.options) {
                if (o.key == QLatin1String("language")) option = &o;
            }
        }
        QVERIFY(option);
        QCOMPARE(option->choices.size(), languages.size());
        QCOMPARE(option->choices.first().value, QStringLiteral("en"));
    }

    void pseudoText() {
        // Accented and longer, in brackets.
        const QString shown = lumenis::ui::pseudoText(QStringLiteral("Save"));
        QVERIFY(shown.startsWith(QLatin1Char('[')) && shown.endsWith(QLatin1Char(']')));
        QVERIFY(!shown.contains(QLatin1String("Save")));
        QVERIFY(shown.size() > QStringLiteral("[Save]").size());
        // Placeholders, the mnemonic, tags and entities are kept as they are, so arg(), shortcuts and rich text work.
        const QString kept = lumenis::ui::pseudoText(QStringLiteral("&Open %1 of %L2 (%n) <b>bold</b> &amp;"));
        for (const char* part : {"&", "%1", "%L2", "%n", "<b>", "</b>", "&amp;"}) QVERIFY2(kept.contains(QLatin1String(part)), part);
        QCOMPARE(lumenis::ui::pseudoText(QStringLiteral("%1")).arg(7).contains(QLatin1Char('7')), true);
        QCOMPARE(lumenis::ui::pseudoText(QString()), QString());
    }

    void resourcesThroughTheTranslator() {
        // Commands: the action's text translated, its id the same.
        OneText command(QStringLiteral("commands"), QStringLiteral("Save Bin"), QStringLiteral("Bin speichern"));
        qApp->installTranslator(&command);
        {
            CommandRegistry commands;
            commands.loadBuiltin();
            QAction* save = commands.action(QStringLiteral("file.save_bin"));
            QVERIFY(save);
            QCOMPARE(save->text(), QStringLiteral("Bin speichern"));
            QCOMPARE(commands.text(QStringLiteral("file.save_bin")), QStringLiteral("Bin speichern"));
        }
        qApp->removeTranslator(&command);
        // Effects: the stored name stays; what is shown is renamed, then translated.
        OneText effect(QStringLiteral("effects"), QStringLiteral("Flow Motion"), QStringLiteral("Flussbewegung"));
        qApp->installTranslator(&effect);
        QCOMPARE(model::shownName(QStringLiteral("FluidMotion")), QStringLiteral("Flussbewegung"));
        qApp->removeTranslator(&effect);
        QCOMPARE(model::shownName(QStringLiteral("FluidMotion")), QStringLiteral("Flow Motion"));
        // Settings: the kind is the identifier, its title translated.
        OneText setting(QStringLiteral("settings"), QStringLiteral("Interface"), QStringLiteral("Oberfläche"));
        qApp->installTranslator(&setting);
        const SettingKind* ui = SettingsSchema::builtIn().find(QStringLiteral("Interface"));
        QVERIFY(ui);
        QVERIFY(ui->title().contains(QStringLiteral("Oberfläche")));
        qApp->removeTranslator(&setting);
    }

    void pseudoLocaleEverywhere() {
        // The pseudo-locale's translator changes every text, ours and the resources'.
        QVERIFY(installLanguage(*qApp, QStringLiteral("pseudo")));
        QCOMPARE(installedLanguage(), QStringLiteral("pseudo"));
        CommandRegistry commands;
        commands.loadBuiltin();
        QAction* save = commands.action(QStringLiteral("file.save_bin"));
        QVERIFY(save && save->text().startsWith(QLatin1Char('[')));
        QVERIFY(model::shownName(QStringLiteral("Dissolve")).startsWith(QLatin1Char('[')));
    }
};

QTEST_MAIN(TestTranslation)
#include "tst_translation.moc"
