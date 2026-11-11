// Audio CDs on import (audio_cd.h; Editing Guide p.215, p.1198).
#include "lumenis/media/audio_cd.h"

#include "lumenis/ext/audio_cd.h"

#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QtEndian>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

#include <algorithm>
#include <memory>
#include <vector>

namespace lumenis::media {

namespace {

std::uint16_t u16(const QByteArray& d, int at) { return qFromLittleEndian<std::uint16_t>(d.constData() + at); }
std::uint32_t u32(const QByteArray& d, int at) { return qFromLittleEndian<std::uint32_t>(d.constData() + at); }

void put16(QByteArray& d, std::uint16_t v) {
    char b[2];
    qToLittleEndian(v, b);
    d.append(b, 2);
}
void put32(QByteArray& d, std::uint32_t v) {
    char b[4];
    qToLittleEndian(v, b);
    d.append(b, 4);
}

// A position in minutes, seconds and frames (1/75 s), as .cda files keep it: frame, second, minute, 0.
std::uint32_t msf(std::uint32_t sectors) {
    const std::uint32_t minutes = sectors / (75 * 60), seconds = (sectors / 75) % 60, frames = sectors % 75;
    return frames | (seconds << 8) | (minutes << 16);
}

} // namespace

bool isCdaFile(const QString& path) { return QFileInfo(path).suffix().compare(QLatin1String("cda"), Qt::CaseInsensitive) == 0; }

std::optional<CdaTrack> parseCda(const QByteArray& d) {
    // "RIFF" size "CDDA" "fmt " 24: version, track, serial, start sector, length in sectors, start and length as MSF.
    if (d.size() < 44 || !d.startsWith("RIFF") || d.mid(8, 4) != "CDDA" || d.mid(12, 4) != "fmt ") return std::nullopt;
    if (u32(d, 16) < 24) return std::nullopt;
    CdaTrack t;
    t.track = u16(d, 22);
    t.serial = u32(d, 24);
    t.startSector = u32(d, 28);
    t.sectors = u32(d, 32);
    if (t.track < 1 || t.track > 99 || t.sectors == 0) return std::nullopt;
    return t;
}

std::optional<CdaTrack> readCdaFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    return parseCda(f.read(256));
}

QByteArray cdaFileData(const CdaTrack& t) {
    QByteArray d("RIFF");
    put32(d, 36);
    d.append("CDDAfmt ");
    put32(d, 24);
    put16(d, 1);
    put16(d, static_cast<std::uint16_t>(t.track));
    put32(d, t.serial);
    put32(d, t.startSector);
    put32(d, t.sectors);
    put32(d, msf(t.startSector + 150)); // a disc's first sector is at 00:02:00
    put32(d, msf(t.sectors));
    return d;
}

bool isOnDisc(const QString& path) {
    if (isCdaFile(path) || path.contains(QLatin1String("cdda:"), Qt::CaseInsensitive)) return true;
    const QStorageInfo volume(QFileInfo(path).absolutePath());
    if (!volume.isValid()) return false;
    const QString fs = QString::fromLatin1(volume.fileSystemType()).toLower();
    return fs == QLatin1String("cddafs") || fs == QLatin1String("iso9660") || fs == QLatin1String("udf") ||
           fs == QLatin1String("cdfs") || fs == QLatin1String("cd9660");
}

bool writePcmWav(const QString& path, const QByteArray& pcm, int sampleRate, int channels) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QByteArray h("RIFF");
    put32(h, static_cast<std::uint32_t>(36 + pcm.size()));
    h.append("WAVEfmt ");
    put32(h, 16);
    put16(h, 1); // PCM
    put16(h, static_cast<std::uint16_t>(channels));
    put32(h, static_cast<std::uint32_t>(sampleRate));
    put32(h, static_cast<std::uint32_t>(sampleRate * channels * 2));
    put16(h, static_cast<std::uint16_t>(channels * 2));
    put16(h, 16);
    h.append("data");
    put32(h, static_cast<std::uint32_t>(pcm.size()));
    return f.write(h) == h.size() && f.write(pcm) == pcm.size();
}

#ifdef _WIN32
namespace {

// Reads ".cda" tracks through the CD-ROM driver's raw read of CD-DA sectors (IOCTL_CDROM_RAW_READ, part of the Windows
// SDK's ntddcdrm.h; its code and argument are spelled out here so that only <windows.h> is needed).
class WindowsAudioCdProvider : public ext::AudioCdProvider {
public:
    ext::ProviderInfo info() const override {
        return {"org.cutthulhu.windows-audio-cd", "Audio CDs (Windows CD-ROM driver)", ext::Category::AudioCd,
                ext::LicenseTier::OperatingSystem, "Cutthulhu", "1"};
    }
    bool readsTrack(const std::string& path) const override {
        const QString p = QString::fromStdString(path);
        return isCdaFile(p) && readCdaFile(p).has_value();
    }
    bool readTrack(const std::string& path, const std::string& wavPath, const std::function<bool(double)>& progress,
                   std::string* error) override {
        const QString p = QString::fromStdString(path);
        const auto track = readCdaFile(p);
        if (!track) {
            if (error) *error = "not a CD track file";
            return false;
        }
        // The drive the file is on: "\\.\D:".
        const QString root = QFileInfo(p).absoluteFilePath().left(2);
        const std::wstring device = (QStringLiteral("\\\\.\\") + root).toStdWString();
        HANDLE h = CreateFileW(device.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (error) *error = "the CD drive cannot be opened";
            return false;
        }
        struct RawReadInfo {
            LARGE_INTEGER diskOffset; // in 2048-byte units of the sector's place
            ULONG sectorCount;
            int trackMode; // CDDA = 2
        };
        constexpr DWORD kRawRead = 0x0002403E; // IOCTL_CDROM_RAW_READ
        constexpr DWORD kSector = 2352;
        constexpr std::uint32_t kChunk = 20;
        QByteArray pcm;
        pcm.reserve(static_cast<qsizetype>(track->sectors) * kSector);
        std::vector<char> buffer(kChunk * kSector);
        bool ok = true;
        for (std::uint32_t done = 0; done < track->sectors && ok;) {
            const std::uint32_t count = std::min(kChunk, track->sectors - done);
            RawReadInfo info{};
            info.diskOffset.QuadPart = static_cast<LONGLONG>(track->startSector + done) * 2048;
            info.sectorCount = count;
            info.trackMode = 2;
            DWORD got = 0;
            if (!DeviceIoControl(h, kRawRead, &info, sizeof(info), buffer.data(), count * kSector, &got, nullptr)) {
                if (error) *error = "the CD cannot be read";
                ok = false;
                break;
            }
            pcm.append(buffer.data(), static_cast<qsizetype>(got));
            done += count;
            if (progress && !progress(static_cast<double>(done) / static_cast<double>(track->sectors))) {
                if (error) *error = "cancelled";
                ok = false;
            }
        }
        CloseHandle(h);
        if (!ok) return false;
        if (!writePcmWav(QString::fromStdString(wavPath), pcm)) {
            if (error) *error = "the sound file cannot be written";
            return false;
        }
        return true;
    }
};

} // namespace
#endif

void registerAudioCdProviders(ext::ProviderRegistry& registry) {
#ifdef _WIN32
    registry.add(std::make_shared<WindowsAudioCdProvider>());
#else
    (void)registry;
#endif
}

} // namespace lumenis::media
