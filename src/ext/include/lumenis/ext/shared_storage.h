// The extension point behind shared workspaces and Distributed Processing (Phase 8.7a2; docs/spec/proxy-media.md; Editing
// Guide p.508, p.510-512, p.514-516). Media Composer offers "a local drive or ... an Avid NEXIS workspace" in the Create
// Proxies window, and Distributed Processing, which "is only available after you select an Avid NEXIS workspace", hands the
// proxy jobs to worker machines; Copy Media copies media "back to a mounted Avid NEXIS workspace" (p.512). Both need the
// vendor's storage client and servers, which are not published, so the application only defines this hook: a module that
// talks to the storage lists its workspaces, which then stand beside the media drives in Create Proxies and Copy Media,
// and may take proxy jobs. The application polls a job's status and gives its clip the proxies when it is done.
// Header-only, standard types only, so that modules can implement it (docs/EXTENSIONS.md).
#pragma once

#include "lumenis/ext/provider.h"

#include <string>
#include <vector>

namespace lumenis::ext {

// One workspace mounted now.
struct SharedWorkspace {
    std::string id;   // stable while it is mounted
    std::string name; // for the drive menus
    std::string root; // the folder it is mounted at; media is written under it as on a media drive
    // Whether proxy jobs can go to Distributed Processing on it (submitProxyJob).
    bool distributedProcessing = false;
};

// A proxy job for one master clip.
struct ProxyJob {
    std::string workspaceId;
    // The clip as the bin keeps it (model::BinItem's JSON, compact), with the link to its media.
    std::string clipJson;
    // The proxy raster the application would use (media::proxyRaster).
    int width = 0;
    int height = 0;
};

// Where a job is.
struct ProxyJobStatus {
    enum class State { Queued, Running, Done, Failed };
    State state = State::Queued;
    double progress = 0.0; // 0 to 1
    // When Done: what the clip keeps of its proxies (model::ProxyMedia's JSON, compact): both files' links, their folder,
    // whether they have sound and their raster.
    std::string proxyJson;
    std::string error; // when Failed, for the Console
};

class SharedStorageProvider : public Provider {
public:
    // The workspaces mounted now.
    virtual std::vector<SharedWorkspace> workspaces() const = 0;
    // Sends a proxy job to Distributed Processing; its id in `jobId`. False with the reason in `error` (the default:
    // this storage has no Distributed Processing).
    virtual bool submitProxyJob(const ProxyJob& job, std::string* jobId, std::string* error) {
        (void)job;
        (void)jobId;
        if (error) *error = "Distributed Processing is not available on this storage";
        return false;
    }
    // Where a job sent with submitProxyJob is; Failed for an id it does not know.
    virtual ProxyJobStatus jobStatus(const std::string& jobId) const {
        (void)jobId;
        ProxyJobStatus s;
        s.state = ProxyJobStatus::State::Failed;
        s.error = "unknown job";
        return s;
    }
};

} // namespace lumenis::ext
