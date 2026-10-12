#pragma once

#include <QString>

namespace lumenis::model {

// Where projects and user profiles live. The defaults mirror the layout the
// Media Composer guide describes (p.67-68), under this product's own names:
//
//                 Private projects               Shared projects                         Users
//   Windows       Documents\<P> Projects         Public\Documents\Shared <P> Projects     Public\Documents\<P>\Users
//   macOS         ~/Documents/<P> Projects       /Users/Shared/<P>/Shared <P> Projects    /Users/Shared/<P>/Users
//   Linux         ~/Documents/<P> Projects       /srv/<P>/Shared <P> Projects             ~/.local/share/<org>/<app>/Users
//
// <P> is the product name. Tests and screenshot runs construct their own.
struct Locations {
    QString privateProjects;
    QString sharedProjects;
    QString users;

    static Locations defaults();
};

} // namespace lumenis::model
