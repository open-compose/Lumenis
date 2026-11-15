// The update check (ROADMAP 10.6; docs/spec/update-check.md): GitHub's releases/latest answer read, versions
// compared, and Help > Check for Updates and the start-up check saying what they found, without the network (a
// fetcher answers) and without downloading anything.
#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/tool_windows.h"
#include "lumenis/ui/update_check.h"

#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

QByteArray release(const char* tag) {
    return QStringLiteral("{\"tag_name\": \"%1\", \"name\": \"Cutthulhu %1\", \"draft\": false, \"prerelease\": false,"
                          " \"html_url\": \"https://github.com/open-compose/lumenis/releases/tag/%1\"}")
        .arg(QLatin1String(tag))
        .toUtf8();
}

} // namespace

class TestUpdateCheck : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void versionsAndAnswers() {
        QCOMPARE(compareVersions(QStringLiteral("0.10.1"), QStringLiteral("0.9.3")), 1);
        QCOMPARE(compareVersions(QStringLiteral("v0.2.0"), QStringLiteral("0.2.0")), 0);
        QCOMPARE(compareVersions(QStringLiteral("0.2"), QStringLiteral("0.2.0")), 0);
        QCOMPARE(compareVersions(QStringLiteral("0.2.0-rc1"), QStringLiteral("0.2.0")), -1);
        QCOMPARE(compareVersions(QStringLiteral("0.2.0-rc2"), QStringLiteral("0.2.0-rc1")), 1);
        QCOMPARE(compareVersions(QStringLiteral("1.0.0"), QStringLiteral("0.99.99")), 1);
        QCOMPARE(latestReleaseUrl(QStringLiteral("https://github.com/open-compose/lumenis")),
                 QUrl(QStringLiteral("https://api.github.com/repos/open-compose/lumenis/releases/latest")));
        QVERIFY(latestReleaseUrl(QStringLiteral("https://example.org/cutthulhu")).isEmpty());

        const auto r = parseLatestRelease(release("v0.3.1"));
        QVERIFY(r);
        QCOMPARE(r->version, QStringLiteral("0.3.1"));
        QCOMPARE(r->page.toString(), QStringLiteral("https://github.com/open-compose/lumenis/releases/tag/v0.3.1"));
        QString error;
        QVERIFY(!parseLatestRelease("not json", &error));
        QVERIFY(!error.isEmpty());
        // A page that is not https is not offered.
        QVERIFY(!parseLatestRelease("{\"tag_name\": \"v1.0.0\", \"html_url\": \"http://example.org\"}"));

        QCOMPARE(updateResultOf(200, release("v0.3.1"), {}, QStringLiteral("0.2.0")).state, UpdateResult::State::Newer);
        QCOMPARE(updateResultOf(200, release("v0.2.0"), {}, QStringLiteral("0.2.0")).state, UpdateResult::State::UpToDate);
        QCOMPARE(updateResultOf(200, release("v0.1.0"), {}, QStringLiteral("0.2.0")).state, UpdateResult::State::UpToDate);
        QCOMPARE(updateResultOf(404, {}, QStringLiteral("not found"), QStringLiteral("0.2.0")).state, UpdateResult::State::NoRelease);
        const UpdateResult offline = updateResultOf(0, {}, QStringLiteral("Host not found"), QStringLiteral("0.2.0"));
        QCOMPARE(offline.state, UpdateResult::State::Failed);
        QCOMPARE(offline.error, QStringLiteral("Host not found"));
        QCOMPARE(updateResultOf(403, {}, {}, QStringLiteral("0.2.0")).state, UpdateResult::State::Failed);
    }

    void checkFromHelpAndAtStartUp() {
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1000, 700);
        window.show();
        QList<QUrl> asked;
        QByteArray answer = release("v9999.1");
        int status = 200;
        window.setUpdateFetcher([&](const QUrl& url, const std::function<void(int, QByteArray, QString)>& reply) {
            asked << url;
            reply(status, answer, status == 0 ? QStringLiteral("Host not found") : QString());
        });
        QList<std::pair<UpdateResult::State, bool>> said;
        window.setUpdateNoticeHook([&](const UpdateResult& r, bool fromMenu) { said << std::pair{r.state, fromMenu}; });

        // Help > Check for Updates asks GitHub's releases/latest for this project.
        QAction* check = commands.action(QStringLiteral("help.check_updates"));
        QVERIFY(check && check->isEnabled());
        check->trigger();
        QCOMPARE(asked.size(), 1);
        QCOMPARE(asked.first(), latestReleaseUrl(QString::fromUtf8(branding::kHomepage)));
        QCOMPARE(said.size(), 1);
        QCOMPARE(said.first().first, UpdateResult::State::Newer);
        QVERIFY(said.first().second);
        QCOMPARE(window.lastUpdatePage().toString(), QStringLiteral("https://github.com/open-compose/lumenis/releases/tag/v9999.1"));
        // At start-up, the same check (not from the menu).
        answer = release(branding::kVersion);
        window.checkForUpdates(false);
        QCOMPARE(said.last(), (std::pair{UpdateResult::State::UpToDate, false}));

        // Without the hook: a newer release opens a notice that does not block, with Open Release Page and Later.
        window.setUpdateNoticeHook({});
        answer = release("v9999.1");
        window.checkForUpdates(false);
        QMessageBox* box = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* b = qobject_cast<QMessageBox*>(w); b && b->isVisible()) box = b;
        }
        QVERIFY(box);
        QVERIFY(box->text().contains(QStringLiteral("9999.1")));
        QVERIFY(!box->isModal() || box->windowModality() != Qt::ApplicationModal);
        QStringList buttons;
        for (QAbstractButton* b : box->buttons()) buttons << b->text();
        QVERIFY2(buttons.contains(QStringLiteral("Open Release Page")) && buttons.contains(QStringLiteral("Later")), qPrintable(buttons.join(',')));
        box->close();
        // No answer at start-up: a Console line only, no message.
        status = 0;
        const qsizetype lines = ConsoleLog::instance().lines().size();
        window.checkForUpdates(false);
        QVERIFY(ConsoleLog::instance().lines().size() > lines);
        QVERIFY(ConsoleLog::instance().lines().last().contains(QStringLiteral("Host not found")));
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* b = qobject_cast<QMessageBox*>(w)) QVERIFY(!b->isVisible() || !b->text().contains(QStringLiteral("Host")));
        }
    }
};

QTEST_MAIN(TestUpdateCheck)
#include "tst_update_check.moc"
