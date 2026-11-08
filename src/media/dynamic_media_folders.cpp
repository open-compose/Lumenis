// Dynamic Media Folders' folders, profiles and the work on one new entry (Editing Guide p.371-376; 8.4b,
// docs/spec/background-services.md). The data round-trips through JSON; the work links the entry's files, fills the
// AIS columns and runs the profile's Actions in order, on the calling thread (the Background Queue's worker).
#include "lumenis/media/dynamic_media_folders.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/media.h"
#include "lumenis/model/multichannel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>

#include <algorithm>

namespace lumenis::media {

namespace {

// Enums as their names in the JSON, so that the file reads well and survives reordering.
template <typename E>
QString enumName(E value, const std::initializer_list<std::pair<E, const char*>>& names) {
    for (const auto& [v, n] : names) {
        if (v == value) return QString::fromLatin1(n);
    }
    return {};
}
template <typename E>
E enumValue(const QString& name, E fallback, const std::initializer_list<std::pair<E, const char*>>& names) {
    for (const auto& [v, n] : names) {
        if (name == QLatin1String(n)) return v;
    }
    return fallback;
}

using Bin = DmfLinkSettings::Bin;
using Naming = DmfLinkSettings::Naming;
using Reel = DmfLinkSettings::Reel;
using FrameCount = DmfLinkSettings::FrameCount;
using Tc = DmfLinkSettings::Timecode;
const std::initializer_list<std::pair<Bin, const char*>> kBins{{Bin::Active, "active"}, {Bin::New, "new"}};
const std::initializer_list<std::pair<Naming, const char*>> kNamings{
    {Naming::Default, "default"}, {Naming::Volume, "volume"}, {Naming::Specify, "specify"}};
const std::initializer_list<std::pair<Reel, const char*>> kReels{{Reel::FileName, "fileName"},
                                                                  {Reel::FolderName, "folderName"}};
const std::initializer_list<std::pair<FrameCount, const char*>> kFrameCounts{{FrameCount::FromZero, "fromZero"},
                                                                              {FrameCount::FromOne, "fromOne"},
                                                                              {FrameCount::Timecode, "timecode"},
                                                                              {FrameCount::FileName, "fileName"}};
const std::initializer_list<std::pair<Tc, const char*>> kTimecodes{{Tc::Embedded, "embedded"}, {Tc::FileName, "fileName"}};

QString timecodeText(std::int64_t frames, const Rational& rate) {
    const TimecodeFormat format = TimecodeFormat::forRate(rate, false);
    const std::int64_t day = format.framesPerDay();
    return QString::fromStdString(Timecode(((frames % day) + day) % day, format).toString());
}

std::optional<std::int64_t> timecodeFrames(const QString& text, const Rational& rate) {
    const auto tc = Timecode::parse(text.toStdString(), TimecodeFormat::forRate(rate, false));
    if (!tc) return std::nullopt;
    return tc->frameCount();
}

// A path under `from` moved under `to` (a copy's path); the path itself when it is not under `from`.
QString movedPath(const QString& path, const QString& from, const QString& to) {
    const QString prefix = QDir::cleanPath(from) + QLatin1Char('/');
    if (!path.startsWith(prefix)) return path;
    return QDir::cleanPath(to) + QLatin1Char('/') + path.mid(prefix.size());
}

// The same for a link's path, which may be an image sequence's spec.
QString movedLinkPath(const QString& path, const QString& from, const QString& to) {
    if (auto seq = ImageSequence::fromSpec(path)) {
        seq->pattern = movedPath(seq->pattern, from, to);
        return seq->spec();
    }
    return movedPath(path, from, to);
}

} // namespace

// ---------------------------------------------------------------- data

QString DmfAction::kindName(Kind kind) {
    switch (kind) {
    case Kind::CopyToFolder: return QStringLiteral("Copy to Folder");
    case Kind::Consolidate: return QStringLiteral("Consolidate");
    case Kind::Transcode: return QStringLiteral("Transcode");
    }
    return {};
}

std::optional<DmfAction::Kind> DmfAction::kindFromName(const QString& name) {
    for (Kind k : {Kind::CopyToFolder, Kind::Consolidate, Kind::Transcode}) {
        if (kindName(k) == name) return k;
    }
    return std::nullopt;
}

QJsonObject DmfAction::toJson() const {
    QJsonObject o{{QStringLiteral("kind"), kindName(kind)}};
    // Only what the kind uses, so that a file shows each action's own options.
    if (kind == Kind::CopyToFolder) {
        o.insert(QStringLiteral("copyTo"), copyTo);
        o.insert(QStringLiteral("autoRelink"), autoRelink);
        o.insert(QStringLiteral("checkin"), checkin);
        return o;
    }
    if (kind == Kind::Consolidate) {
        o.insert(QStringLiteral("skipOnTarget"), skipOnTarget);
        o.insert(QStringLiteral("relinkBeforeSkip"), relinkBeforeSkip);
    } else {
        o.insert(QStringLiteral("resolution"), resolution);
        o.insert(QStringLiteral("reformat"), reformat);
        o.insert(QStringLiteral("colorTransforms"), colorTransforms);
    }
    o.insert(QStringLiteral("sampleRate"), sampleRate);
    o.insert(QStringLiteral("bitDepth"), bitDepth);
    o.insert(QStringLiteral("audioFormat"), audioFormat);
    o.insert(QStringLiteral("videoDrive"), videoDrive);
    o.insert(QStringLiteral("audioDrive"), audioDrive);
    return o;
}

DmfAction DmfAction::fromJson(const QJsonObject& json) {
    DmfAction a;
    a.kind = kindFromName(json.value(QStringLiteral("kind")).toString()).value_or(Kind::CopyToFolder);
    a.copyTo = json.value(QStringLiteral("copyTo")).toString();
    a.autoRelink = json.value(QStringLiteral("autoRelink")).toBool(true);
    a.checkin = json.value(QStringLiteral("checkin")).toBool();
    a.skipOnTarget = json.value(QStringLiteral("skipOnTarget")).toBool();
    a.relinkBeforeSkip = json.value(QStringLiteral("relinkBeforeSkip")).toBool();
    a.sampleRate = json.value(QStringLiteral("sampleRate")).toInt();
    a.bitDepth = json.value(QStringLiteral("bitDepth")).toInt();
    a.audioFormat = json.value(QStringLiteral("audioFormat")).toString();
    a.videoDrive = json.value(QStringLiteral("videoDrive")).toString();
    a.audioDrive = json.value(QStringLiteral("audioDrive")).toString();
    a.resolution = json.value(QStringLiteral("resolution")).toString();
    a.reformat = json.value(QStringLiteral("reformat")).toBool();
    a.colorTransforms = json.value(QStringLiteral("colorTransforms")).toBool();
    return a;
}

QJsonObject DmfLinkSettings::toJson() const {
    return {{QStringLiteral("plugin"), plugin},
            {QStringLiteral("bin"), enumName(bin, kBins)},
            {QStringLiteral("naming"), enumName(naming, kNamings)},
            {QStringLiteral("binName"), binName},
            {QStringLiteral("multichannel"), multichannel},
            {QStringLiteral("trackFormats"), trackFormats},
            {QStringLiteral("audioStartTime"), audioStartTime},
            {QStringLiteral("reel"), enumName(reel, kReels)},
            {QStringLiteral("reelLevel"), reelLevel},
            {QStringLiteral("frameCount"), enumName(frameCount, kFrameCounts)},
            {QStringLiteral("timecode"), enumName(timecode, kTimecodes)},
            {QStringLiteral("defaultFps"), QStringLiteral("%1/%2").arg(defaultFps.num()).arg(defaultFps.den())}};
}

DmfLinkSettings DmfLinkSettings::fromJson(const QJsonObject& json) {
    DmfLinkSettings s;
    s.plugin = json.value(QStringLiteral("plugin")).toString(s.plugin);
    s.bin = enumValue(json.value(QStringLiteral("bin")).toString(), s.bin, kBins);
    s.naming = enumValue(json.value(QStringLiteral("naming")).toString(), s.naming, kNamings);
    s.binName = json.value(QStringLiteral("binName")).toString();
    s.multichannel = json.value(QStringLiteral("multichannel")).toBool();
    s.trackFormats = json.value(QStringLiteral("trackFormats")).toString();
    s.audioStartTime = json.value(QStringLiteral("audioStartTime")).toBool();
    s.reel = enumValue(json.value(QStringLiteral("reel")).toString(), s.reel, kReels);
    s.reelLevel = std::clamp(json.value(QStringLiteral("reelLevel")).toInt(1), 1, 8);
    s.frameCount = enumValue(json.value(QStringLiteral("frameCount")).toString(), s.frameCount, kFrameCounts);
    s.timecode = enumValue(json.value(QStringLiteral("timecode")).toString(), s.timecode, kTimecodes);
    const QStringList fps = json.value(QStringLiteral("defaultFps")).toString().split(QLatin1Char('/'));
    if (fps.size() == 2 && fps[0].toLongLong() > 0 && fps[1].toLongLong() > 0) {
        s.defaultFps = Rational(fps[0].toLongLong(), fps[1].toLongLong());
    }
    return s;
}

QJsonObject DmfProfile::toJson() const {
    QJsonArray list;
    for (const DmfAction& a : actions) list.append(a.toJson());
    return {{QStringLiteral("name"), name}, {QStringLiteral("link"), link.toJson()}, {QStringLiteral("actions"), list}};
}

DmfProfile DmfProfile::fromJson(const QJsonObject& json) {
    DmfProfile p;
    p.name = json.value(QStringLiteral("name")).toString();
    p.link = DmfLinkSettings::fromJson(json.value(QStringLiteral("link")).toObject());
    for (const QJsonValue& v : json.value(QStringLiteral("actions")).toArray()) p.actions << DmfAction::fromJson(v.toObject());
    return p;
}

QJsonObject DmfFolder::toJson() const {
    return {{QStringLiteral("path"), path},
            {QStringLiteral("profile"), profile},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("done"), QJsonArray::fromStringList(done)}};
}

DmfFolder DmfFolder::fromJson(const QJsonObject& json) {
    DmfFolder f;
    f.path = json.value(QStringLiteral("path")).toString();
    f.profile = json.value(QStringLiteral("profile")).toString();
    f.enabled = json.value(QStringLiteral("enabled")).toBool();
    for (const QJsonValue& v : json.value(QStringLiteral("done")).toArray()) f.done << v.toString();
    return f;
}

const DmfProfile* DynamicMediaFolders::profile(const QString& name) const {
    for (const DmfProfile& p : profiles) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

DmfFolder* DynamicMediaFolders::folder(const QString& path) {
    const QString clean = QDir::cleanPath(path);
    for (DmfFolder& f : folders) {
        if (QDir::cleanPath(f.path) == clean) return &f;
    }
    return nullptr;
}

void DynamicMediaFolders::storeProfile(const DmfProfile& p) {
    for (DmfProfile& existing : profiles) {
        if (existing.name == p.name) {
            existing = p;
            return;
        }
    }
    profiles << p;
}

bool DynamicMediaFolders::addFolder(const QString& path) {
    if (folder(path)) return false;
    DmfFolder f;
    f.path = QDir::cleanPath(path);
    folders << f;
    return true;
}

void DynamicMediaFolders::removeFolder(const QString& path) {
    const QString clean = QDir::cleanPath(path);
    folders.erase(std::remove_if(folders.begin(), folders.end(),
                                 [&](const DmfFolder& f) { return QDir::cleanPath(f.path) == clean; }),
                  folders.end());
}

QJsonObject DynamicMediaFolders::toJson() const {
    QJsonArray p;
    for (const DmfProfile& profile : profiles) p.append(profile.toJson());
    QJsonArray f;
    for (const DmfFolder& folder : folders) f.append(folder.toJson());
    return {{QStringLiteral("version"), 1}, {QStringLiteral("profiles"), p}, {QStringLiteral("folders"), f}};
}

DynamicMediaFolders DynamicMediaFolders::fromJson(const QJsonObject& json) {
    DynamicMediaFolders d;
    for (const QJsonValue& v : json.value(QStringLiteral("profiles")).toArray()) d.profiles << DmfProfile::fromJson(v.toObject());
    for (const QJsonValue& v : json.value(QStringLiteral("folders")).toArray()) d.folders << DmfFolder::fromJson(v.toObject());
    return d;
}

DynamicMediaFolders DynamicMediaFolders::load(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

bool DynamicMediaFolders::save(const QString& file, QString* error) const {
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(toJson()).toJson());
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- entries

QStringList newDmfEntries(const DmfFolder& folder) {
    QDir dir(folder.path);
    QStringList out;
    for (const QFileInfo& info : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (info.fileName().startsWith(QLatin1Char('.')) || folder.done.contains(info.fileName())) continue;
        out << info.fileName();
    }
    return out;
}

QStringList dmfEntryFiles(const QString& entryPath) {
    const QFileInfo info(entryPath);
    if (info.isFile()) return {info.absoluteFilePath()};
    QStringList files;
    QDirIterator it(entryPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!QFileInfo(path).fileName().startsWith(QLatin1Char('.'))) files << path;
    }
    std::sort(files.begin(), files.end());
    return files;
}

QString volumeName(const QString& path) {
#ifdef Q_OS_WIN
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (abs.size() >= 2 && abs[1] == QLatin1Char(':')) return abs.left(2).toUpper();
#endif
    const QStorageInfo storage(path);
    const QString root = storage.isValid() ? storage.rootPath() : QStringLiteral("/");
    if (root == QLatin1String("/")) return root;
    const QString name = QFileInfo(root).fileName();
    return name.isEmpty() ? root : name;
}

QString dmfBinName(const DmfLinkSettings& link, const QString& folderPath, const QString& project, const QStringList& taken) {
    if (link.naming == Naming::Specify && !link.binName.trimmed().isEmpty()) return link.binName.trimmed();
    if (link.naming == Naming::Volume) return volumeName(folderPath);
    // "uses the project name for the bin (bin name followed by a consecutive number)".
    const QString base = project.isEmpty() ? QStringLiteral("Bin") : project;
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!taken.contains(name)) return name;
    }
}

// ---------------------------------------------------------------- AIS columns

QString dpxInputDevice(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray magic = f.read(4);
    if (magic != "SDPX" && magic != "XPDS") return {};
    if (!f.seek(1556)) return {};
    const QByteArray field = f.read(32);
    const qsizetype end = field.indexOf('\0');
    return QString::fromLatin1(end < 0 ? field : field.left(end)).trimmed();
}

std::optional<std::int64_t> timecodeFromName(const QString& file, const Rational& editRate) {
    const QString base = QFileInfo(file).completeBaseName();
    static const QRegularExpression pairs(QStringLiteral("(?<!\\d)(\\d{2})[:_.;-](\\d{2})[:_.;-](\\d{2})[:_.;-](\\d{2})(?!\\d)"));
    static const QRegularExpression eight(QStringLiteral("(?<!\\d)(\\d{2})(\\d{2})(\\d{2})(\\d{2})(?!\\d)"));
    for (const QRegularExpression* re : {&pairs, &eight}) {
        std::optional<std::int64_t> last;
        for (auto it = re->globalMatch(base); it.hasNext();) {
            const auto m = it.next();
            const QString text = QStringLiteral("%1:%2:%3:%4").arg(m.captured(1), m.captured(2), m.captured(3), m.captured(4));
            if (const auto frames = timecodeFrames(text, editRate)) last = frames;
        }
        if (last) return last;
    }
    return std::nullopt;
}

void applyAisColumns(model::BinItem& clip, const QString& file, const DmfLinkSettings& link) {
    const QFileInfo info(file);
    // Labroll: the file's name, or a folder's `reelLevel` up ("one folder (that contains the DPX files) up to eight
    // folders higher"). "If the source file name is only numeric characters, no data will appear".
    QString reel;
    if (link.reel == Reel::FileName) {
        // A numbered still's name without its number ("A001_0001000.dpx" -> "A001").
        static const QRegularExpression trailing(QStringLiteral("[._-]?\\d+$"));
        reel = info.completeBaseName();
        if (isNumberedStill(file)) reel.remove(trailing);
        static const QRegularExpression digits(QStringLiteral("^\\d*$"));
        if (digits.match(reel).hasMatch()) reel.clear();
    } else {
        QDir dir = info.dir();
        for (int i = 1; i < link.reelLevel && dir.cdUp(); ++i) {
        }
        reel = dir.dirName();
    }
    if (!reel.isEmpty()) clip.fields.insert(QStringLiteral("Labroll"), reel);
    // Camroll from a DPX file's Input Device field; empty when it has none.
    if (const QString device = dpxInputDevice(file); !device.isEmpty()) clip.fields.insert(QStringLiteral("Camroll"), device);

    // Start from the file name.
    const Rational rate = clip.editRate;
    if (link.timecode == Tc::FileName) {
        if (const auto start = timecodeFromName(file, rate)) {
            clip.fields.insert(QStringLiteral("Start"), timecodeText(*start, rate));
            clip.fields.insert(QStringLiteral("End"), timecodeText(*start + clip.durationFrames, rate));
        }
    }

    // DPX: "a 7 digit number", the prefix "derived from the Labroll bin column" (DPX files only).
    if (info.suffix().compare(QLatin1String("dpx"), Qt::CaseInsensitive) != 0) return;
    std::optional<std::int64_t> count;
    switch (link.frameCount) {
    case FrameCount::FromZero: count = 0; break;
    case FrameCount::FromOne: count = 1; break;
    case FrameCount::Timecode:
        count = timecodeFrames(clip.fields.value(QStringLiteral("Start")), rate);
        break;
    case FrameCount::FileName: {
        static const QRegularExpression number(QStringLiteral("(\\d+)$"));
        const auto m = number.match(info.completeBaseName());
        if (m.hasMatch()) count = m.captured(1).toLongLong();
        break;
    }
    }
    if (count) {
        const QString prefix = clip.fields.value(QStringLiteral("Labroll"));
        clip.fields.insert(QStringLiteral("DPX"), prefix + QStringLiteral("%1").arg(*count, 7, 10, QLatin1Char('0')));
    }
}

// ---------------------------------------------------------------- processing

DmfResult processDmfEntry(const QString& entryPath, const DmfProfile& profile, const DmfProcessOptions& options,
                          const std::function<bool(double)>& progress) {
    DmfResult result;
    const QStringList files = dmfEntryFiles(entryPath);
    if (files.isEmpty()) {
        result.error = QObject::tr("%1 holds no files.").arg(entryPath);
        return result;
    }
    const DmfLinkSettings& link = profile.link;
    // Stills are numbered at the Default FPS (p.374, "if there is no FPS in the DPX header file").
    const int steps = static_cast<int>(profile.actions.size()) + 1;
    auto report = [&](int step, double within) {
        return !progress || progress((step + std::clamp(within, 0.0, 1.0)) / steps);
    };
    QList<model::BinItem> linked;
    const QStringList entries = groupStills(files, link.defaultFps);
    for (int i = 0; i < entries.size(); ++i) {
        if (!report(0, double(i) / static_cast<double>(entries.size()))) {
            result.error = QObject::tr("Cancelled");
            return result;
        }
        QString why;
        const auto info = probe(entries[i], &why);
        auto clip = info ? linkClip(*info, options.link, &why) : std::nullopt;
        if (!clip) {
            result.messages << QObject::tr("%1 is not media that can be linked.").arg(QFileInfo(entries[i]).fileName());
            continue;
        }
        // Audio Start-Time Option: a BWF file starts at the frame its time stamp falls in.
        if (link.audioStartTime && info) {
            const auto l = linkOf(*clip);
            if (l && l->videoStream < 0) {
                for (const StreamInfo& s : info->streams) {
                    if (s.kind != StreamKind::Audio || s.sampleRate <= 0) continue;
                    if (const auto tr = bwfTimeReference(*info)) {
                        const BwfAlignment a = alignToFrame(*tr, s.sampleRate, clip->editRate);
                        clip->fields.insert(QStringLiteral("Start"), timecodeText(a.startFrame, clip->editRate));
                        clip->fields.insert(QStringLiteral("End"), timecodeText(a.startFrame + clip->durationFrames, clip->editRate));
                    }
                    break;
                }
            }
        }
        if (link.multichannel && !link.trackFormats.isEmpty()) {
            QString formatError;
            const auto groups = model::parseTrackFormats(link.trackFormats);
            if (!groups) {
                result.messages << QObject::tr("Multichannel Audio: \"%1\" is not a list of track formats.").arg(link.trackFormats);
            } else if (!model::setTrackFormats(*clip, *groups, &formatError)) {
                result.messages << formatError;
            }
        }
        const auto l = linkOf(*clip);
        applyAisColumns(*clip, l ? l->file() : entries[i], link);
        linked << *clip;
    }
    if (linked.isEmpty()) {
        result.error = QObject::tr("%1 holds no media that can be linked.").arg(QFileInfo(entryPath).fileName());
        return result;
    }

    QList<model::BinItem> made; // Consolidate's and Transcode's ".new" clips
    QStringList taken = options.taken;
    for (int step = 0; step < profile.actions.size(); ++step) {
        const DmfAction& action = profile.actions[step];
        if (!report(step + 1, 0)) {
            result.error = QObject::tr("Cancelled");
            break;
        }
        if (action.kind == DmfAction::Kind::CopyToFolder) {
            if (action.copyTo.isEmpty()) {
                result.messages << QObject::tr("Copy to Folder has no folder set.");
                continue;
            }
            // The entry is copied under the folder with its own name, so that its files keep their structure.
            const QString from = QFileInfo(entryPath).absolutePath();
            bool failed = false;
            for (int i = 0; i < files.size() && !failed; ++i) {
                const QString to = movedPath(QFileInfo(files[i]).absoluteFilePath(), from, action.copyTo);
                QDir().mkpath(QFileInfo(to).absolutePath());
                if (QFileInfo::exists(to)) QFile::remove(to);
                if (!QFile::copy(files[i], to)) {
                    result.error = QObject::tr("%1 could not be copied to %2.").arg(files[i], action.copyTo);
                    failed = true;
                }
                if (!report(step + 1, double(i + 1) / static_cast<double>(files.size()))) {
                    result.error = QObject::tr("Cancelled");
                    failed = true;
                }
            }
            if (failed) break;
            if (action.autoRelink) {
                for (model::BinItem& clip : linked) {
                    const auto l = linkOf(clip);
                    if (!l) continue;
                    QString why;
                    if (!relinkClip(clip, movedLinkPath(l->path, from, action.copyTo), &why)) result.messages << why;
                }
            }
            continue;
        }
        ConsolidateOptions o;
        o.mode = action.kind == DmfAction::Kind::Transcode ? ConsolidateOptions::Mode::Transcode
                                                           : ConsolidateOptions::Mode::Consolidate;
        o.skipOnTarget = action.skipOnTarget;
        // The clips keep the linked media; ".new" clips take the new (p.376).
        o.keepMastersLinked = true;
        o.creation = options.creation;
        if (!action.videoDrive.isEmpty()) o.creation.videoDrive = action.videoDrive;
        if (!action.audioDrive.isEmpty()) o.creation.audioDrive = action.audioDrive;
        if (action.sampleRate > 0) o.creation.sampleRate = action.sampleRate;
        if (action.bitDepth == 16 || action.bitDepth == 24) o.creation.bitDepth = action.bitDepth;
        if (o.mode == ConsolidateOptions::Mode::Transcode && !action.resolution.isEmpty()) {
            if (const Resolution* r = findResolution(action.resolution)) o.creation.resolution = *r;
        }
        o.creation.progress = [&report, step](double f) { return report(step + 1, f); };
        const QHash<QString, model::BinItem> byId = [&] {
            QHash<QString, model::BinItem> h;
            for (const model::BinItem& c : linked) h.insert(c.id, c);
            return h;
        }();
        const ClipLookup lookup = [&byId](const QString&, const QString& id) -> std::optional<model::BinItem> {
            const auto it = byId.constFind(id);
            if (it == byId.constEnd()) return std::nullopt;
            return *it;
        };
        const ConsolidateResult r = consolidate(linked, QString(), lookup, taken, o);
        for (const model::BinItem& c : r.added) {
            made << c;
            taken << c.name;
        }
        result.messages << r.skipped;
        if (!r.error.isEmpty()) {
            result.error = r.error;
            break;
        }
    }
    result.clips = linked + made;
    return result;
}

} // namespace lumenis::media
