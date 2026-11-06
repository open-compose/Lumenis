// The built-in Export to Device > XDCAM device (Phase 7.2b; docs/spec/export-device.md; Editing Guide p.883-884): an
// XDCAM disk seen as a folder, as a Sony drive's file access mode mounts it or as the user names it. Exported clips go
// in its Clip folder with the next sequential name, "All exported clips are given a new sequential name of Cxxxx.mxf,
// for example, C0019.mxf" (the deck's convention; here we number them).
#pragma once

#include "lumenis/ext/export_device.h"

#include <QString>
#include <QStringList>

namespace lumenis::media {

// Folders to treat as XDCAM disks besides the mounted volumes that look like one (tests, and disks copied to a drive):
// set here or in LUMENIS_XDCAM_DISKS (separated like PATH). With LUMENIS_NO_SYSTEM_DRIVES=1 only these are listed.
void setXdcamDiskFolders(const QStringList& folders);
QStringList xdcamDiskFolders();

// The disks: the folders above, and mounted volumes whose root has a Clip folder beside a General, Sub or Edit folder
// (the XDCAM disk layout, ours by observation).
QStringList xdcamDisks();
// The next free clip name in a disk's Clip folder: C0001.MXF, or one past the highest Cnnnn there.
QString nextXdcamClipName(const QString& disk);

class XdcamDiskProvider : public ext::ExportDeviceProvider {
public:
    ext::ProviderInfo info() const override;
    std::string deviceKind() const override { return "xdcam"; }
    std::vector<ext::ExportDeviceTarget> targets() const override;
    // XDCAM HD: XDCAM-50, -35, -25 and -17 (p.884).
    std::vector<std::string> videoFormats() const override;
    bool deliver(const std::string& targetId, const std::string& file, std::string* clipName, std::string* error) override;
};

} // namespace lumenis::media
