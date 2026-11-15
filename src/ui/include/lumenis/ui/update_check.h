#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

class QNetworkAccessManager;

// The update check (ROADMAP 10.6, Phase 12.1; docs/spec/update-check.md). It asks GitHub for the latest release of the
// project (its REST API's releases/latest, one GET with no account and nothing about the user) and says whether it is
// newer than the version running. It only tells: nothing is downloaded or installed (the owner's decision,
// 2026-11-14: "editors talking to the owner do not want an update during an important session"). MainWindow runs it
// at start-up when the Interface setting's "Check for Updates at Startup" is on, and from Help > Check for Updates.
namespace lumenis::ui {

// A release as GitHub describes it.
struct ReleaseInfo {
    QString tag;     // "v0.2.0"
    QString version; // "0.2.0": the tag without a leading "v"
    QString name;    // the release's title; the tag when it has none
    QUrl page;       // its page on GitHub (html_url)
};

// What a check found.
struct UpdateResult {
    enum class State {
        Newer,     // `release` is newer than the version running
        UpToDate,  // the latest release is this version or older
        NoRelease, // the project has no release yet (GitHub answers 404)
        Failed,    // no answer, or one that could not be read: `error` says why
    };
    State state = State::Failed;
    ReleaseInfo release;
    QString current; // the version running
    QString error;
};

// The latest release in GitHub's JSON for releases/latest; nothing, with `error`, when it is not one. Drafts and
// pre-releases are not "latest" on GitHub, so they never come here.
std::optional<ReleaseInfo> parseLatestRelease(const QByteArray& json, QString* error = nullptr);
// Compares two versions ("0.10.1" > "0.9.3"), a leading "v" ignored: numbers by number, a version with a suffix
// ("0.2.0-rc1") before the same one without. -1, 0 or 1.
int compareVersions(const QString& a, const QString& b);
// The API address of the latest release for a GitHub project page ("https://github.com/owner/repo"); empty when the
// page is not on GitHub.
QUrl latestReleaseUrl(const QString& homepage);
// The result for an HTTP answer: its status (0 for no connection), body and network error text.
UpdateResult updateResultOf(int status, const QByteArray& body, const QString& networkError, const QString& current);

class UpdateChecker : public QObject {
    Q_OBJECT

public:
    // `current` is the version running; `endpoint` the releases/latest address.
    UpdateChecker(QString current, QUrl endpoint, QObject* parent = nullptr);
    ~UpdateChecker() override;

    // Asks once; finished() follows (later, or at once when there is no endpoint or a fetcher answers at once).
    void check();
    // Tests: answers in place of the network, with (status, body, error).
    using Fetcher = std::function<void(const QUrl&, const std::function<void(int, QByteArray, QString)>&)>;
    void setFetcher(Fetcher fetcher) { fetcher_ = std::move(fetcher); }

signals:
    void finished(const lumenis::ui::UpdateResult& result);

private:
    QString current_;
    QUrl endpoint_;
    Fetcher fetcher_;
    QNetworkAccessManager* network_ = nullptr;
};

} // namespace lumenis::ui
