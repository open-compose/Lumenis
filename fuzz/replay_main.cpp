// lumenis_fuzz_replay: the fuzz targets without libFuzzer (fuzz_targets.h), so that saved inputs run
// through them with any compiler, on every platform (ctest fuzz_replay). It also writes the seeds
// made by our own writers (seeds.cpp) for scripts/fuzz.py.
//
//   lumenis_fuzz_replay [--target NAME]... [--seeds] PATH...
//       Runs every file under each PATH (a file or a folder, searched recursively) through each
//       target named (all of them when none is), and with --seeds the built-in seeds through their
//       own target. A crash or failed check aborts, naming the input first.
//   lumenis_fuzz_replay --make-seeds DIR [AAF...]
//       Writes the built-in seeds to DIR/<target>/, and each AAF file given made smaller
//       (compactAaf) to DIR/aaf/ and DIR/cfb/.
#include "fuzz_targets.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cstdio>

using namespace lumenis::fuzz;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: lumenis_fuzz_replay [--target NAME]... [--seeds] PATH...\n"
                 "       lumenis_fuzz_replay --make-seeds DIR [AAF...]\n"
                 "targets:");
    for (const Target& t : targets()) std::fprintf(stderr, " %s", t.name);
    std::fprintf(stderr, "\n");
    return 2;
}

// Runs one input through one target, saying which first, so that a crash's last line names it.
void run(const Target& t, const QString& name, const QByteArray& data) {
    std::fprintf(stderr, "%s: %s\n", t.name, qPrintable(name));
    std::fflush(stderr);
    t.run(reinterpret_cast<const std::uint8_t*>(data.constData()), static_cast<std::size_t>(data.size()));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); // arguments as QStrings, in the platform's encoding
    QStringList args = app.arguments().mid(1);
    quietQt();

    if (args.size() >= 2 && args[0] == QLatin1String("--make-seeds")) {
        int written = 0;
        auto save = [&](const QString& target, const Seed& s) {
            const QString dir = QDir(args[1]).filePath(target);
            QFile f(QDir(dir).filePath(s.name));
            if (!QDir().mkpath(dir) || !f.open(QIODevice::WriteOnly) || f.write(s.data) != s.data.size()) {
                std::fprintf(stderr, "cannot write %s\n", qPrintable(f.fileName()));
                return false;
            }
            ++written;
            return true;
        };
        for (const Target& t : targets()) {
            for (const Seed& s : seeds(QString::fromLatin1(t.name))) {
                if (!save(QString::fromLatin1(t.name), s)) return 1;
            }
        }
        for (const QString& path : args.mid(2)) {
            QFile in(path);
            if (!in.open(QIODevice::ReadOnly)) {
                std::fprintf(stderr, "cannot read %s\n", qPrintable(path));
                return 1;
            }
            for (const Seed& s : compactAaf(QFileInfo(path).completeBaseName(), in.readAll())) {
                if (!save(QStringLiteral("aaf"), s) || !save(QStringLiteral("cfb"), s)) return 1;
            }
        }
        std::printf("%d seeds written to %s\n", written, qPrintable(args[1]));
        return 0;
    }

    std::vector<Target> chosen;
    bool withSeeds = false;
    QStringList paths;
    for (qsizetype i = 0; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--target") && i + 1 < args.size()) {
            const QString name = args[++i];
            bool found = false;
            for (const Target& t : targets()) {
                if (name == QLatin1String(t.name)) {
                    chosen.push_back(t);
                    found = true;
                }
            }
            if (!found) return usage();
        } else if (args[i] == QLatin1String("--seeds")) {
            withSeeds = true;
        } else if (args[i].startsWith(QLatin1String("--"))) {
            return usage();
        } else {
            paths << args[i];
        }
    }
    if (chosen.empty()) chosen = targets();
    if (paths.isEmpty() && !withSeeds) return usage();

    QElapsedTimer timer;
    timer.start();
    int inputs = 0;
    if (withSeeds) {
        for (const Target& t : chosen) {
            for (const Seed& s : seeds(QString::fromLatin1(t.name))) {
                run(t, QStringLiteral("(seed) ") + s.name, s.data);
                ++inputs;
            }
        }
    }
    QStringList files;
    for (const QString& p : paths) {
        const QFileInfo info(p);
        if (info.isFile()) {
            files << p;
        } else if (info.isDir()) {
            QDirIterator it(p, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext()) files << it.next();
        } else {
            std::fprintf(stderr, "skipping %s: not found\n", qPrintable(p));
        }
    }
    files.sort();
    for (const QString& path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "cannot read %s\n", qPrintable(path));
            return 1;
        }
        const QByteArray data = f.readAll();
        for (const Target& t : chosen) {
            run(t, path, data);
            ++inputs;
        }
    }
    std::printf("%d runs (%lld files, %zu targets) in %.1f s, no crash\n", inputs, static_cast<long long>(files.size()),
                chosen.size(), static_cast<double>(timer.elapsed()) / 1000.0);
    return 0;
}
