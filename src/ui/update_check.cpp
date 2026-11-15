// The update check (lumenis/ui/update_check.h; docs/spec/update-check.md): GitHub's releases/latest through
// QNetworkAccessManager, read into an UpdateResult.
#include "lumenis/ui/update_check.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslSocket>
#include <QStringList>

namespace lumenis::ui {

namespace {

QString withoutV(QString v) {
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V'))) v.remove(0, 1);
    return v;
}

} // namespace

std::optional<ReleaseInfo> parseLatestRelease(const QByteArray& json, QString* error) {
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    if (!doc.isObject()) {
        if (error) *error = QCoreApplication::translate("UpdateCheck", "GitHub's answer could not be read.");
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    ReleaseInfo r;
    r.tag = o.value(QStringLiteral("tag_name")).toString();
    r.version = withoutV(r.tag);
    r.name = o.value(QStringLiteral("name")).toString();
    if (r.name.isEmpty()) r.name = r.tag;
    r.page = QUrl(o.value(QStringLiteral("html_url")).toString());
    if (r.version.isEmpty() || !r.page.isValid() || r.page.scheme() != QLatin1String("https")) {
        if (error) *error = QCoreApplication::translate("UpdateCheck", "GitHub's answer names no release.");
        return std::nullopt;
    }
    return r;
}

int compareVersions(const QString& a, const QString& b) {
    // "1.2.3-rc1": the numbers, then the suffix.
    auto split = [](const QString& v, QList<int>* numbers, QString* suffix) {
        const QString clean = withoutV(v);
        const qsizetype dash = clean.indexOf(QLatin1Char('-'));
        *suffix = dash < 0 ? QString() : clean.mid(dash + 1);
        for (const QString& part : clean.left(dash < 0 ? clean.size() : dash).split(QLatin1Char('.'))) {
            static const QRegularExpression digits(QStringLiteral("^\\d+"));
            const QRegularExpressionMatch m = digits.match(part);
            *numbers << (m.hasMatch() ? m.captured().toInt() : 0);
        }
    };
    QList<int> na, nb;
    QString sa, sb;
    split(a, &na, &sa);
    split(b, &nb, &sb);
    for (qsizetype i = 0; i < std::max(na.size(), nb.size()); ++i) {
        const int x = na.value(i);
        const int y = nb.value(i);
        if (x != y) return x < y ? -1 : 1;
    }
    if (sa == sb) return 0;
    if (sa.isEmpty()) return 1; // a release after its candidates
    if (sb.isEmpty()) return -1;
    return sa < sb ? -1 : 1;
}

QUrl latestReleaseUrl(const QString& homepage) {
    const QUrl page(homepage);
    if (page.host() != QLatin1String("github.com")) return {};
    const QStringList parts = page.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2) return {};
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest").arg(parts.at(0), parts.at(1)));
}

UpdateResult updateResultOf(int status, const QByteArray& body, const QString& networkError, const QString& current) {
    UpdateResult r;
    r.current = current;
    if (status == 404) {
        // "No release yet" (and a private repository looks the same).
        r.state = UpdateResult::State::NoRelease;
        return r;
    }
    if (status != 200) {
        r.state = UpdateResult::State::Failed;
        r.error = !networkError.isEmpty() ? networkError
                                          : QCoreApplication::translate("UpdateCheck", "GitHub answered %1.").arg(status);
        return r;
    }
    QString why;
    const auto release = parseLatestRelease(body, &why);
    if (!release) {
        r.state = UpdateResult::State::Failed;
        r.error = why;
        return r;
    }
    r.release = *release;
    r.state = compareVersions(release->version, current) > 0 ? UpdateResult::State::Newer : UpdateResult::State::UpToDate;
    return r;
}

UpdateChecker::UpdateChecker(QString current, QUrl endpoint, QObject* parent)
    : QObject(parent), current_(std::move(current)), endpoint_(std::move(endpoint)) {}

UpdateChecker::~UpdateChecker() = default;

void UpdateChecker::check() {
    if (!endpoint_.isValid()) {
        UpdateResult r;
        r.current = current_;
        r.error = QCoreApplication::translate("UpdateCheck", "This build names no GitHub project to check.");
        emit finished(r);
        return;
    }
    if (fetcher_) {
        fetcher_(endpoint_, [this](int status, const QByteArray& body, const QString& error) {
            emit finished(updateResultOf(status, body, error, current_));
        });
        return;
    }
    if (!QSslSocket::supportsSsl()) {
        // Qt's TLS plugin is missing from the package (BUILDING.md "Update check"): say so rather than fail quietly.
        UpdateResult r;
        r.current = current_;
        r.error = QCoreApplication::translate("UpdateCheck", "No secure connection is available (Qt's TLS plug-in is missing).");
        emit finished(r);
        return;
    }
    if (!network_) network_ = new QNetworkAccessManager(this);
    QNetworkRequest request(endpoint_);
    // What GitHub's API asks of clients: its media type and a User-Agent, here only the product and version.
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("%1/%2").arg(QCoreApplication::applicationName(), current_));
    request.setTransferTimeout(15000);
    QNetworkReply* reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        emit finished(updateResultOf(status, reply->readAll(), error, current_));
    });
}

} // namespace lumenis::ui
