# Release gate (ROADMAP 10.8, Phase 12.3)

The last check before a release is published: Phase 10's steps on each platform, the gates the phases left open with
their reasons, and the owner's own try of the release candidate on each operating system. Tick a box with the date;
an item that cannot pass names why and who decides. The draft release is the one `.github/workflows/release.yml`
made (BUILDING.md "Releases").

## 1. The release run

| Check | macOS arm64 | Windows x64 | Linux x64 |
|---|---|---|---|
| The release workflow's build and `ctest` (release codec policy) passed | [ ] | [ ] | [ ] AppImage [ ] .deb |
| The installer gate passed (installed or mounted, `--smoke-test`, no x264/x265 inside) | [ ] | [ ] | [ ] |
| The package and its symbols are on the draft; `SHA256SUMS.txt` checks (`sha256sum -c`) | [ ] | [ ] | [ ] |
| The hosted CI jobs on the release commit: Xcode 16 and 26; Windows; Ubuntu 24.04 and 26.04 | [ ] | [ ] | [ ] |

## 2. Phase 10's steps

| Step | State | What is left |
|---|---|---|
| 10.1 performance | done (10.1b 2026-11-18, in 2026.2) | the audio mix on macOS and Windows (about twice Linux's time) is profiled only if editors report slow audio (docs/spec/performance.md) |
| 10.2 long-run stability | done but the runs | 72-hour soaks on each OS (Windows passed 2026-11-17; Linux stopped at 64.6 hours, again; macOS) and the sanitizer soaks (docs/spec/soak.md "The runs"), the owner's |
| 10.3 accessibility | done but the hand check | docs/checklists/screen-readers.md with VoiceOver, Narrator or NVDA, and Orca, the owner's |
| 10.4 localisation | done but the reviews | native speakers' reviews of German, French, Italian and Irish (translations/glossary_*.md) |
| 10.5 crash reports | done | a crash from the release package reads as functions and lines with its symbols (BUILDING.md "Crash reports") |
| 10.6 update check | done (12.1), checked 2026-11-19: 2026.1 offered 2026.2 (the owner) | — |
| 10.7 release pipeline | done (12.2): 2026.1 published 2026-11-16, 2026.2 2026-11-19 (its run passed first time), 2026.3 2026-11-19 (macOS re-run for want of a runner) | section 1 above |
| 10.0 going public | done (2026-11-15) | the community files after §2.2; run 423, stuck queued, deleted from the Actions page or by GitHub Support |

## 3. Gates the phases left open

Each with its reason (ROADMAP's gate status for that phase).

* **Phase 0, gate 4, Linux:** a person following BUILDING.md on a fresh Linux machine and trying the packages by hand
  (macOS and Windows passed on 2026-10-12).
* **Phase 1, gate 5:** the editor's session with docs/checklists/phase1-gate5.md.
* **Phase 2, gate 2's Media Composer half:** Media Composer linking our managed MXF media; waits for the owner's
  licensed seat (`scripts/check-mxf.py --make`). Gates 3 and 4 (playback) passed on the owner's three machines.
* **Phase 3:** the Media Composer halves of gates 2 and 3 (bins and AAF through Media Composer and back) wait for the
  seat; 3.5 (`.avp` projects, `.avs` settings) is deferred: no prior art, samples or seat.
* **Phase 4:** gate 1's expected sequences from real Media Composer wait for the seat; gate 3 is by hand.
* **Phase 5:** the renders from real Media Composer to compare against wait for the seat.
* **Phase 7:** 7.3c, the AJA and Blackmagic modules, needs their SDKs and hardware; validating DCPs with a checker
  waits for asdcplib on a runner; the video output hardware gate waits for hardware.
* **Phase 11:** XAVC Intra and DNxUncompressed are not made (no open encoder); see ROADMAP's Phase 11 status.
* **Phase 12:** the questions for counsel in docs/spec/release-codecs.md "Watch" (FFmpeg's decoder front ends kept for
  the hardware decoders; BtbN's LGPL Windows build carrying FFmpeg's own AAC and ProRes encoders, which the policy
  refuses).

## 4. The owner's try of the release candidate

On each operating system, from the draft release's package, as an editor would:

| Step | macOS | Windows | Linux (AppImage) |
|---|---|---|---|
| The download's checksum matches `SHA256SUMS.txt` (INSTALL.md "Checking a download") | [ ] | [ ] | [ ] |
| It installs and starts past the OS's warning for unsigned programs (INSTALL.md) | [ ] | [ ] | [ ] |
| A new project; media linked and imported (DNx managed media made) | [ ] | [ ] | [ ] |
| An H.264 clip plays (the OS's decoder), or says why it cannot | [ ] | [ ] | [ ] |
| A sequence edited, played with sound, rendered | [ ] | [ ] | [ ] |
| Export to File: an H.264 MP4 (the OS's encoder; not offered on Linux) and a DNxHR MOV | [ ] | [ ] | [ ] |
| Help > Check for Updates answers (no release yet, or the latest) | [ ] | [ ] | [ ] |
| Help > About names the version; the Console has no failures | [ ] | [ ] | [ ] |
| It uninstalls (macOS: to the Bin; Windows: Settings > Apps; Linux: delete the AppImage) | [ ] | [ ] | [ ] |

When every row passes or names its reason, publish the draft release.
