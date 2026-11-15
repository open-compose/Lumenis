#pragma once

#include <QString>

#include <optional>

namespace lumenis::model {

// Where projects and user profiles live. The defaults mirror the layout the
// Media Composer guide describes (p.67-68), under this product's own names:
//
//                 Private projects               Shared projects                         Users
//   Windows       Documents\<P> Projects         Public\Documents\Shared <P> Projects     Public\Documents\<P>\Users
//   macOS         ~/Documents/<P> Projects       /Users/Shared/<P>/Shared <P> Projects    /Users/Shared/<P>/Users
//   Linux         ~/Documents/<P> Projects       /srv/<P>/Shared <P> Projects             ~/.local/share/<org>/<app>/Users
//
// Site settings (p.1137) are in a Settings folder beside Users, and the attic (p.71, p.298: "Avid
// Attic" beside the Users folder's parent) is "<P> Attic" there too.
//
// <P> is the product name. Tests and screenshot runs construct their own.
struct Locations {
    // Absolute folders, cleaned (QDir::cleanPath).
    QString privateProjects;
    QString sharedProjects;
    QString users;
    QString siteSettings; // empty: no site settings
    QString attic;        // bin backups (model::saveToAttic); empty: none

    // The defaults for this platform, from the table above. The folders are not created here.
    static Locations defaults();
};

// "Automatically launch last project at startup" (the Interface setting, p.1200; "To open a project automatically",
// p.69): the user and the project file opened last, kept in the users folder as "Last Project.json" ({"user",
// "project"}; ours). Nothing when there is none or it cannot be read.
struct LastProject {
    QString user;
    QString file;
};
std::optional<LastProject> readLastProject(const Locations& locations);
// Writes it, making the users folder if need be; false when it cannot be written.
bool writeLastProject(const Locations& locations, const LastProject& last);

} // namespace lumenis::model
