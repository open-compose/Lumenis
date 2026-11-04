#pragma once

// The interchange corpus for the interop tests (scripts/interop-corpus.py, BUILDING.md "The interchange corpus").
// ctest's interop_corpus_fetch downloads it into CUTTHULHU_INTEROP_CORPUS and writes ".complete" there once every
// file is in and checked. A download cut off part way (a runner's network dropping) leaves folders half full; the
// tests then skip, as they do without a network, instead of failing on files that never came.

#include <QDir>
#include <QFileInfo>
#include <QString>

namespace lumenis::test {

// The corpus folder, or empty when it is not set or not completely downloaded.
inline QString interopCorpus() {
    const QString folder = qEnvironmentVariable("CUTTHULHU_INTEROP_CORPUS");
    if (folder.isEmpty() || !QFileInfo::exists(QDir(folder).filePath(QStringLiteral(".complete")))) return {};
    return folder;
}

} // namespace lumenis::test
