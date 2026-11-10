#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

// Copy Media (Editing Guide p.512-513; 8.7a2, docs/spec/proxy-media.md): the files of the selected items' managed media,
// proxies and renders, and their bins, gathered onto one drive or into one folder. The application works out which files
// belong to the selection (MainWindow::copyMediaFiles); this plans where each goes, what it needs and copies them.
// "Copy Media copies managed media. It will not copy Linked media" (p.512): linked files are never given here.
namespace lumenis::media {

// What to copy and where.
struct CopyMediaRequest {
    // Absolute paths, each list without repeats.
    QStringList managedFiles; // the clips' managed media (MXF and the WAV beside it)
    QStringList proxyFiles;   // their proxies (model::ProxyMedia)
    QStringList renderFiles;  // the sequences' render precomputes
    QStringList binFiles;     // the bins' files
    // The dialog's boxes: "Bins, Managed media, Proxy media, and Renders" (p.513).
    bool bins = true;
    bool managed = true;
    bool proxies = true;
    bool renders = true;
    // "Choose whether to copy the media to a Drive or Folder" (p.513). A drive is a media drive's root (MediaDrive::root)
    // or a shared workspace's (ext::SharedWorkspace::root); a folder is any folder.
    bool toFolder = false;
    QString target;
    // "Copy Bins to" (p.513): the folder the "Avid Copied Bins" folder is made in.
    QString binsTarget;
};

// Where each file goes, worked out without writing anything.
struct CopyMediaPlan {
    struct Copy {
        QString from;
        QString to;
        std::int64_t bytes = 0;
    };
    QList<Copy> media; // managed media, proxies and renders
    QList<Copy> bins;
    std::int64_t mediaBytes = 0;
    std::int64_t binBytes = 0;
    // Files skipped because a file of the same name and size is already at the target (ours: "All selected files already
    // exist at the target location", p.513).
    int existing = 0;
    // Files that are not there to copy (offline media).
    QStringList missing;
    // The folders the files go in: on a drive "Avid MediaFiles/MXF/<next number>" for managed media and renders and
    // "Avid MediaFiles/Proxy/<next number>" for proxies; in a folder its "Avid Copied Media" for both (p.513); bins in
    // binsTarget's "Avid Copied Bins".
    QString mediaFolder;
    QString proxyFolder;
    QString binFolder;
    bool isEmpty() const { return media.isEmpty() && bins.isEmpty(); }
};

// The next numbered folder under `parent`: one more than the highest number there, "1" when there is none ("Managed media
// is placed into a new folder within MXF and the folder is named using the next available number", p.513).
QString nextNumberedFolder(const QString& parent);

CopyMediaPlan planCopyMedia(const CopyMediaRequest& request);

// The space free on the volume `path` is on (the nearest folder that exists), -1 when it cannot be found.
std::int64_t availableSpace(const QString& path);

// Whether the plan's media and bins fit in the space free at the target and the bins' target ("If there is not enough
// space, copying will not be allowed", p.513). Both on one volume add up.
bool planFits(const CopyMediaPlan& plan, const CopyMediaRequest& request);

// Makes the folders and copies the plan's files. `progress` gets 0 to 1 and returns false to stop. Returns the files
// copied; what could not be copied goes in `warnings`.
int copyMedia(const CopyMediaPlan& plan, const std::function<bool(double)>& progress = {}, QStringList* warnings = nullptr);

// "4.36 MB" and "82.73 GB", as the dialog shows sizes (p.513).
QString megabytes(std::int64_t bytes);
QString gigabytes(std::int64_t bytes);

} // namespace lumenis::media
