#pragma once

#include <QByteArray>
#include <QFile>
#include <QSaveFile>
#include <QString>
#include <QStringDecoder>
#include <QStringList>

#include <map>
#include <optional>

// What the readers and writers of text lists share (edl.cpp, ale.cpp): reading a file whose encoding
// is not stated, splitting it into lines whatever the line ends, writing atomically, and warnings
// counted once each. Internal to lumenis_interop: not installed with the public headers.
namespace lumenis::interop::detail {

// The text of a list: UTF-8 when it is valid UTF-8 (a byte order mark is dropped), else Latin-1,
// which any byte sequence is. Media Composer writes ASCII ("Media Composer only accepts text files
// (ASCII format)", Editing Guide p.114), which both read the same.
inline QString decodeText(const QByteArray& bytes) {
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8.decode(bytes);
    if (utf8.hasError()) text = QString::fromLatin1(bytes);
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    return text;
}

// A file's text (decodeText()); nullopt, with `error` set, when it cannot be read.
inline std::optional<QString> readText(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return std::nullopt;
    }
    return decodeText(file.readAll());
}

// Lines ending in CR LF, LF or CR (classic Mac OS: "[Return] (Mac)", p.107), without their ends.
inline QStringList textLines(const QString& text) {
    QString t = text;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList lines = t.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines;
}

// Writes `text` as UTF-8, replacing the file only once all of it is written.
inline bool writeText(const QString& path, const QString& text, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(text.toUtf8());
    if (!file.commit()) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

// Warnings counted once each: "3 × events past the limit" (as the AAF and bin imports write them),
// in the order of their text.
class Notes {
public:
    void add(const QString& what) { ++notes_[what]; }
    QStringList list() const {
        QStringList out;
        for (const auto& [what, count] : notes_) {
            out << (count > 1 ? QStringLiteral("%1 × %2").arg(count).arg(what) : what);
        }
        return out;
    }

private:
    std::map<QString, int> notes_;
};

} // namespace lumenis::interop::detail
