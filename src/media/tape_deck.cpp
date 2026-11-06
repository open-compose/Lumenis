// The built-in record deck, a tape kept in a folder (lumenis/media/tape_deck.h).
#include "lumenis/media/tape_deck.h"

#include "lumenis/media/media.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>

#include <algorithm>

namespace lumenis::media {

namespace {

QStringList& extraDecks() {
    static QStringList folders;
    return folders;
}

QString modeName(ext::DeckRecordMode mode) {
    switch (mode) {
    case ext::DeckRecordMode::Insert: return QStringLiteral("insert");
    case ext::DeckRecordMode::Assemble: return QStringLiteral("assemble");
    case ext::DeckRecordMode::Crash: return QStringLiteral("crash");
    }
    return QStringLiteral("insert");
}

QJsonObject readTape(const QString& folder) {
    QFile f(QDir(folder).filePath(QStringLiteral("tape.json")));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool writeTape(const QString& folder, const QJsonObject& tape) {
    QSaveFile f(QDir(folder).filePath(QStringLiteral("tape.json")));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(tape).toJson());
    return f.commit();
}

class FolderTapeSession : public ext::DeckSession {
public:
    explicit FolderTapeSession(QString folder) : folder_(std::move(folder)), position_(tapePosition(folder_)) {}
    ~FolderTapeSession() override {
        if (writer_) halt();
    }

    std::int64_t position() const override { return position_; }
    bool cue(std::int64_t frame) override {
        position_ = std::max<std::int64_t>(0, frame);
        storePosition();
        return true;
    }

    bool beginRecord(const ext::DeckRecording& r, std::string* error) override {
        if (writer_) return fail(error, QObject::tr("The deck is recording already."));
        recording_ = r;
        if (r.mode == ext::DeckRecordMode::Crash || r.in < 0) recording_.in = position_;
        QJsonObject tape = readTape(folder_);
        const int n = static_cast<int>(tape.value(QStringLiteral("recordings")).toArray().size()) + 1;
        file_ = QStringLiteral("rec_%1.mov").arg(n, 4, 10, QLatin1Char('0'));
        WriterSettings w;
        w.format = QStringLiteral("mov");
        if (r.video) {
            w.videoCodec = QStringLiteral("png");
            w.pixelFormat = QStringLiteral("rgb24");
            w.width = r.width;
            w.height = r.height;
        }
        w.frameRate = Rational(r.rateNum, std::max(1, r.rateDen));
        if (r.channels > 0 && !r.audio.empty()) {
            w.audioCodec = QStringLiteral("pcm_s24le");
            w.channels = r.channels;
            w.sampleRate = 48000;
        }
        if (w.videoCodec.isEmpty() && w.audioCodec.isEmpty()) return fail(error, QObject::tr("No tracks are enabled to record onto."));
        writer_ = std::make_unique<MediaWriter>();
        QString why;
        if (!writer_->open(QDir(folder_).filePath(file_), w, &why)) {
            writer_.reset();
            return fail(error, why);
        }
        frames_ = 0;
        return true;
    }

    bool writeFrame(const unsigned char* rgba, int stride, const float* sound, int samples, std::string* error) override {
        if (!writer_) return fail(error, QObject::tr("The deck is not recording."));
        if (recording_.video && rgba) {
            const QImage frame(rgba, recording_.width, recording_.height, stride, QImage::Format_RGBA8888);
            if (!writer_->writeVideo(frame)) return fail(error, writer_->lastError());
        }
        if (!recording_.audio.empty() && sound && samples > 0 && !writer_->writeAudio(sound, samples)) return fail(error, writer_->lastError());
        ++frames_;
        return true;
    }

    bool finish(std::string* error) override {
        if (!writer_) return true;
        QString why;
        const bool ok = writer_->finish(&why);
        writer_.reset();
        if (!ok) return fail(error, why);
        return store(error);
    }

    void halt() override {
        if (!writer_) return;
        writer_->finish();
        writer_.reset();
        store(nullptr);
    }

private:
    static bool fail(std::string* error, const QString& text) {
        if (error) *error = text.toStdString();
        return false;
    }

    void storePosition() {
        QJsonObject tape = readTape(folder_);
        tape.insert(QStringLiteral("position"), static_cast<double>(position_));
        writeTape(folder_, tape);
    }

    // The recording made: added to the tape; an assemble edit blanks the tape after its OUT (recordings that start
    // later go, ones running past it are cut short).
    bool store(std::string* error) {
        if (frames_ == 0) {
            QFile::remove(QDir(folder_).filePath(file_));
            return true;
        }
        QJsonObject tape = readTape(folder_);
        QJsonArray list = tape.value(QStringLiteral("recordings")).toArray();
        const std::int64_t out = recording_.in + frames_;
        if (recording_.mode == ext::DeckRecordMode::Assemble) {
            QJsonArray kept;
            for (const QJsonValue& v : list) {
                QJsonObject o = v.toObject();
                const auto in = static_cast<std::int64_t>(o.value(QStringLiteral("in")).toDouble());
                const auto length = static_cast<std::int64_t>(o.value(QStringLiteral("length")).toDouble());
                if (in >= out) continue;
                if (in + length > out) o.insert(QStringLiteral("length"), static_cast<double>(out - in));
                kept << o;
            }
            list = kept;
        }
        QJsonArray audio;
        for (int t : recording_.audio) audio << t;
        list << QJsonObject{{QStringLiteral("in"), static_cast<double>(recording_.in)},
                            {QStringLiteral("length"), static_cast<double>(frames_)},
                            {QStringLiteral("file"), file_},
                            {QStringLiteral("video"), recording_.video},
                            {QStringLiteral("audio"), audio},
                            {QStringLiteral("mode"), modeName(recording_.mode)}};
        tape.insert(QStringLiteral("recordings"), list);
        position_ = out;
        tape.insert(QStringLiteral("position"), static_cast<double>(position_));
        if (!writeTape(folder_, tape)) return fail(error, QObject::tr("The tape's index could not be written."));
        return true;
    }

    QString folder_;
    std::int64_t position_ = 0;
    ext::DeckRecording recording_;
    QString file_;
    std::int64_t frames_ = 0;
    std::unique_ptr<MediaWriter> writer_;
};

} // namespace

void setTapeDeckFolders(const QStringList& folders) { extraDecks() = folders; }

QStringList tapeDeckFolders() {
    QStringList out = extraDecks();
    const QString env = qEnvironmentVariable("LUMENIS_TAPE_DECKS");
    if (!env.isEmpty()) out << env.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    return out;
}

QList<TapeRecording> tapeRecordings(const QString& folder) {
    QList<TapeRecording> out;
    for (const QJsonValue& v : readTape(folder).value(QStringLiteral("recordings")).toArray()) {
        const QJsonObject o = v.toObject();
        TapeRecording r;
        r.in = static_cast<std::int64_t>(o.value(QStringLiteral("in")).toDouble());
        r.length = static_cast<std::int64_t>(o.value(QStringLiteral("length")).toDouble());
        r.file = o.value(QStringLiteral("file")).toString();
        r.video = o.value(QStringLiteral("video")).toBool(true);
        for (const QJsonValue& t : o.value(QStringLiteral("audio")).toArray()) r.audio << t.toInt();
        const QString mode = o.value(QStringLiteral("mode")).toString();
        r.mode = mode == QLatin1String("assemble") ? ext::DeckRecordMode::Assemble
                 : mode == QLatin1String("crash")  ? ext::DeckRecordMode::Crash
                                                   : ext::DeckRecordMode::Insert;
        out << r;
    }
    return out;
}

QImage tapeFrameAt(const QString& folder, std::int64_t frame) {
    const QList<TapeRecording> all = tapeRecordings(folder);
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        if (!it->video || frame < it->in || frame >= it->in + it->length) continue;
        VideoReader reader;
        if (!reader.open(QDir(folder).filePath(it->file))) return {};
        return reader.frame(frame - it->in);
    }
    return {};
}

std::int64_t tapePosition(const QString& folder) {
    return static_cast<std::int64_t>(readTape(folder).value(QStringLiteral("position")).toDouble());
}

ext::ProviderInfo FolderTapeDeckProvider::info() const {
    return {"org.cutthulhu.folder-tape", "Tapes kept in folders", ext::Category::Deck, ext::LicenseTier::OpenSource, "Cutthulhu", "1"};
}

std::vector<ext::DeckInfo> FolderTapeDeckProvider::decks() const {
    std::vector<ext::DeckInfo> out;
    for (const QString& f : tapeDeckFolders()) {
        ext::DeckInfo d;
        d.id = QFileInfo(f).absoluteFilePath().toStdString();
        d.name = QFileInfo(f).fileName().toStdString();
        d.online = QFileInfo(f).isDir();
        out.push_back(std::move(d));
    }
    return out;
}

std::unique_ptr<ext::DeckSession> FolderTapeDeckProvider::open(const std::string& id, std::string* error) {
    const QString folder = QString::fromStdString(id);
    if (!QFileInfo(folder).isDir()) {
        if (error) *error = QObject::tr("The deck %1 is not connected.").arg(QDir::toNativeSeparators(folder)).toStdString();
        return nullptr;
    }
    return std::make_unique<FolderTapeSession>(folder);
}

} // namespace lumenis::media
