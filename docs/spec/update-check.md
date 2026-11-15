# The update check (ROADMAP 10.6, Phase 12.1)

The owner's decision (2026-11-14): no automatic updates, because "editors talking to the owner do not want an update
during an important session". Cutthulhu asks GitHub for the latest release, and when it is newer than the one running
it says so and links to its page. The user decides whether and when to download and install it. Nothing is downloaded
or installed by the application. Sparkle, WinSparkle and AppImageUpdate are not used. Releases are GitHub Releases
(the owner's answer on update hosting, 2026-11-12). Not in Media Composer: this is ours.

## What is asked (`src/ui/update_check.*`)

* One HTTPS GET of GitHub's REST API, `https://api.github.com/repos/<owner>/<repo>/releases/latest`, the project
  being the branding's homepage (`PRODUCT_HOMEPAGE` in cmake/Branding.cmake, `latestReleaseUrl`). No account and
  nothing about the user are sent: the headers are the API's media type and a User-Agent of the product and version
  ("Cutthulhu/2026.1"), as GitHub asks of clients.
* `releases/latest` is the newest release that is neither a draft nor a pre-release. Its `tag_name` ("v0.2.0", a
  leading "v" dropped) is compared with the version running (`compareVersions`: numbers by number, so 0.10 is after
  0.9; a release after its candidates, "0.2.0-rc1" before "0.2.0"). Its `html_url` must be https to be offered.
* Answers (`updateResultOf`): 200 with a newer version, Newer; with this version or an older one, UpToDate; 404,
  NoRelease (no release yet, and a private repository looks the same); anything else, or no answer within 15 seconds,
  Failed with the reason.
* HTTPS needs Qt's TLS backend, a plug-in (`plugins/tls`): windeployqt and macdeployqt bundle it with Qt Network, and
  `packaging/linux/make-appimage.sh` copies it into the AppImage and stops when there is none. Without one the check
  says "No secure connection is available" instead of failing quietly (BUILDING.md "Update check").

## When, and what is said (`MainWindow::checkForUpdates`, `main_window_updates.cpp`)

* **At start-up**, five seconds after the window opens, when the Interface setting's **Check for Updates at Startup**
  (ours, on by default) is on. Only a newer release is said, in a notice that does not block (Open Release Page,
  Later); no release, the latest version, or no answer (offline, a firewall) are a Console line only, since someone
  starting work needs no message about them. `LUMENIS_NO_UPDATE_CHECK=1` skips it: every ctest has it, as does the
  soak.
* **Help > Check for Updates...** (`help.check_updates`) says every answer: the notice for a newer release, "You have
  the latest version" or "There is no release to compare with yet", or the failure in a message (`reportFailure`).
* Open Release Page opens the release's page in the browser (`QDesktopServices::openUrl`); Later closes the notice.

## Tests

`tst_update_check`: versions compared, GitHub's answer read (and refused when it names no https page), each answer's
state; Help > Check for Updates and the start-up check through a fetcher answering in place of the network, the
notice's buttons, and a failed start-up check leaving only a Console line.
