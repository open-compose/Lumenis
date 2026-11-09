#pragma once

#include "lumenis/core/project_format.h"
#include "lumenis/core/rational.h"

#include <QDateTime>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

namespace lumenis::model {

// Everything chosen in New Project (guide p.63-67), plus bookkeeping.
struct ProjectSettings {
    QString name;
    // "Choose for Me": raster and edit rate are fixed when the first clip is
    // added to a sequence, not now (p.64). format is then only provisional.
    bool chooseForMe = false;
    ProjectFormat format;
    // Picture aspect ratio (ProjectFormat::defaultAspect() unless chosen otherwise).
    Rational aspect{16, 9};
    // Texts from colorSpacesFor(), colorDepths() and stereoscopicModes() in project_format.h.
    QString colorSpace = QStringLiteral("YCbCr 709");
    QString colorDepth = QStringLiteral("10 bit");
    QString stereoscopic = QStringLiteral("Off");
    QString filmType; // empty when the project is not a film project
    // The Format tab's Proxy (Editing Guide p.526-527, p.549-550; 8.5e2): 0 for none, 4 or 16 to play and render the
    // sequence at 1/4 or 1/16 of the project's resolution ("you can play your sequence at 1/4 or 1/16 of the current
    // project resolution", p.526). Only high-resolution projects have it (proxyModes).
    int proxy = 0;
    // Stored in UTC; create() fills them in when they are invalid.
    QDateTime created;
    QDateTime modified;
};

// The proxy modes a format offers (p.526-527): 0 (none) always; for high-resolution formats ("The proxy mode is only
// available for high-resolution projects", p.526) 4 and 16 while the smaller raster keeps "The minimum dimensions ... 256
// pixels in width by 120 pixels in height" (p.65, p.355: "in a 960x540 project, only 1/4-proxy (480x270) will be
// available and not 1/16-proxy (240x135)").
QList<int> proxyModes(const ProjectFormat& format);
// The raster pictures and renders are made at in a proxy mode: half the project's width and height for 1/4, a quarter
// for 1/16 (even); the project's raster for none or a mode the format does not offer.
QSize proxyRaster(const ProjectSettings& settings);

// A project found in or created by a ProjectStore.
struct ProjectEntry {
    QString folder; // the project folder
    QString file;   // the project file inside it
    ProjectSettings settings;
};

// A folder of projects (Private, Shared or an external folder). Each
// project is a folder named after the project holding "<name>.cthproj",
// a JSON file. Media Composer's own .avp/.avb files are read and written by
// the interchange layer (Phase 3), not here.
class ProjectStore {
public:
    static QString fileSuffix() { return QStringLiteral(".cthproj"); }

    // root is the folder holding the project folders; it need not exist yet.
    explicit ProjectStore(QString root);
    const QString& root() const { return root_; }

    // Projects in the folder, sorted by name (case-insensitive). Folders
    // without a readable project file are skipped.
    QList<ProjectEntry> list() const;

    // Creates the folder (and the root if needed) and the project file.
    // Fails if the name is invalid or a project of that name exists.
    std::optional<ProjectEntry> create(ProjectSettings settings, QString* error = nullptr) const;

    // Deletes the project folder and everything in it (its bins).
    // Media files are not touched (they live elsewhere).
    bool remove(const QString& name, QString* error = nullptr) const;

    // The project file (format "cutthulhu-project", version 1). read() fails for a file from a newer
    // version or one missing its name, format family, edit rate or aspect ratio. write() replaces
    // the file atomically.
    static std::optional<ProjectSettings> read(const QString& file, QString* error = nullptr);
    static bool write(const QString& file, const ProjectSettings& settings, QString* error = nullptr);

private:
    QString root_;
};

// User profiles (guide p.68): one folder per user under the Users folder.
class UserProfiles {
public:
    explicit UserProfiles(QString root);

    // The profile folder names, sorted case-insensitively.
    QStringList list() const;
    // Creates the profile folder. Names follow the project naming rules (validateName()).
    bool create(const QString& name, QString* error = nullptr) const;
    // The operating-system login name, as Media Composer uses by default.
    static QString defaultName();

private:
    QString root_;
};

} // namespace lumenis::model
