// OpenColorIO behind ocio_colour.h (6.11c3): the config, colour space lookups and cached CPU processors. Everything is
// behind one mutex; the OFX host already serialises its renders, so this costs nothing there.
#include "ocio_colour.h"

#include <QByteArray>
#include <QHash>
#include <QPair>

#include <mutex>

#ifdef LUMENIS_HAVE_OCIO
#include <OpenColorIO/OpenColorIO.h>
#endif

namespace lumenis::media::detail {

#ifdef LUMENIS_HAVE_OCIO
namespace OCIO = OCIO_NAMESPACE;

namespace {

struct State {
    std::mutex mutex;
    QString requested;        // setOcioConfig's
    bool loaded = false;      // the config below was looked for
    QString config, error;    // what was loaded, or why not
    OCIO::ConstConfigRcPtr ocio;
    QHash<QPair<QString, QString>, OCIO::ConstCPUProcessorRcPtr> processors;
};

State& state() {
    static State s;
    return s;
}

// Where the automatic config comes from, in order.
QStringList automaticConfigs() {
    QStringList out;
    const QByteArray env = qgetenv("OCIO");
    if (!env.isEmpty()) out << QString::fromLocal8Bit(env);
#if OCIO_VERSION_HEX >= 0x02030000
    out << QStringLiteral("ocio://studio-config-v2.1.0_aces-v1.3_ocio-v2.3");
#endif
#if OCIO_VERSION_HEX >= 0x02020000
    out << QStringLiteral("ocio://studio-config-v1.0.0_aces-v1.3_ocio-v2.1");
#endif
    return out;
}

// Loads the config if not yet done; the caller holds the mutex.
void load(State& s) {
    if (s.loaded) return;
    s.loaded = true;
    s.config.clear();
    s.error.clear();
    s.ocio.reset();
    s.processors.clear();
    if (qEnvironmentVariableIntValue("LUMENIS_NO_OCIO") != 0) return;
    const QStringList candidates = s.requested.isEmpty() ? automaticConfigs() : QStringList{s.requested};
    for (const QString& c : candidates) {
        try {
            OCIO::ConstConfigRcPtr config = OCIO::Config::CreateFromFile(c.toUtf8().constData());
            if (!config) continue;
            s.ocio = config;
            s.config = c;
            s.error.clear();
            return;
        } catch (const std::exception& e) {
            if (s.error.isEmpty()) s.error = QStringLiteral("%1: %2").arg(c, QString::fromUtf8(e.what()));
        }
    }
}

const char* nameOf(const OCIO::ConstConfigRcPtr& config, const QString& name) {
    if (!config || name.isEmpty()) return nullptr;
    const OCIO::ConstColorSpaceRcPtr cs = config->getColorSpace(name.toUtf8().constData());
    return cs ? cs->getName() : nullptr;
}

} // namespace

bool ocioBuilt() {
    return true;
}

QString ocioVersion() {
    return QString::fromUtf8(OCIO::GetVersion());
}

void setOcioConfig(const QString& config) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.requested = config;
    s.loaded = false;
}

QString ocioConfig(QString* error) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    load(s);
    if (error) *error = s.error;
    return s.config;
}

QString ocioColourSpace(const QString& name) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    load(s);
    try {
        const char* n = nameOf(s.ocio, name);
        return n ? QString::fromUtf8(n) : QString();
    } catch (const std::exception&) {
        return {};
    }
}

bool ocioIsData(const QString& name) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    load(s);
    try {
        if (!s.ocio || name.isEmpty()) return false;
        const OCIO::ConstColorSpaceRcPtr cs = s.ocio->getColorSpace(name.toUtf8().constData());
        return cs && cs->isData();
    } catch (const std::exception&) {
        return false;
    }
}

bool ocioConvert(float* rgba, std::size_t pixels, const QString& from, const QString& to, QString* error) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    load(s);
    if (!s.ocio) {
        if (error) *error = s.error.isEmpty() ? QStringLiteral("no OpenColorIO config") : s.error;
        return false;
    }
    if (from == to || pixels == 0) return true;
    try {
        const auto key = qMakePair(from, to);
        OCIO::ConstCPUProcessorRcPtr cpu = s.processors.value(key);
        if (!cpu) {
            const OCIO::ConstProcessorRcPtr processor = s.ocio->getProcessor(from.toUtf8().constData(), to.toUtf8().constData());
            cpu = processor->getDefaultCPUProcessor();
            s.processors.insert(key, cpu);
        }
        // Unpremultiply the partly transparent pixels, convert, premultiply again.
        for (std::size_t i = 0; i < pixels; ++i) {
            float* p = rgba + i * 4;
            if (p[3] > 0.0f && p[3] < 1.0f) {
                p[0] /= p[3];
                p[1] /= p[3];
                p[2] /= p[3];
            }
        }
        OCIO::PackedImageDesc image(rgba, static_cast<long>(pixels), 1, 4);
        cpu->apply(image);
        for (std::size_t i = 0; i < pixels; ++i) {
            float* p = rgba + i * 4;
            if (p[3] > 0.0f && p[3] < 1.0f) {
                p[0] *= p[3];
                p[1] *= p[3];
                p[2] *= p[3];
            } else if (p[3] <= 0.0f) {
                p[0] = p[1] = p[2] = 0.0f;
            }
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
}

bool ocioBakeFile(const QString& path, int size, float* rgb, QString* error) {
    if (size < 2) return false;
    try {
        OCIO::FileTransformRcPtr file = OCIO::FileTransform::Create();
        file->setSrc(path.toUtf8().constData());
        file->setInterpolation(OCIO::INTERP_LINEAR);
        const OCIO::ConstConfigRcPtr raw = OCIO::Config::CreateRaw();
        const OCIO::ConstCPUProcessorRcPtr cpu = raw->getProcessor(file)->getDefaultCPUProcessor();
        const std::size_t n = static_cast<std::size_t>(size);
        for (std::size_t b = 0; b < n; ++b) {
            for (std::size_t g = 0; g < n; ++g) {
                for (std::size_t r = 0; r < n; ++r) {
                    float* p = rgb + ((b * n + g) * n + r) * 3;
                    p[0] = static_cast<float>(r) / static_cast<float>(n - 1);
                    p[1] = static_cast<float>(g) / static_cast<float>(n - 1);
                    p[2] = static_cast<float>(b) / static_cast<float>(n - 1);
                }
            }
        }
        OCIO::PackedImageDesc image(rgb, static_cast<long>(n * n * n), 1, 3);
        cpu->apply(image);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
}

#else // without OpenColorIO

bool ocioBuilt() {
    return false;
}
QString ocioVersion() {
    return {};
}
void setOcioConfig(const QString&) {}
QString ocioConfig(QString* error) {
    if (error) error->clear();
    return {};
}
QString ocioColourSpace(const QString&) {
    return {};
}
bool ocioIsData(const QString&) {
    return false;
}
bool ocioConvert(float*, std::size_t, const QString&, const QString&, QString* error) {
    if (error) *error = QStringLiteral("this build has no OpenColorIO");
    return false;
}

bool ocioBakeFile(const QString&, int, float*, QString* error) {
    if (error) *error = QStringLiteral("CLF files need OpenColorIO, which this build does not have");
    return false;
}

#endif

} // namespace lumenis::media::detail
