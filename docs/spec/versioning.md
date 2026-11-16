# Versions (Phase 12)

The owner's decision (2026-11-15): versions are **YEAR.N**, the year of the release and that year's release number,
counting from 1: the first release of 2026 is 2026.1, the next 2026.2, and the fiftieth of 2027 is 2027.50.

* **Nobody edits a version for a release.** The release workflow (`.github/workflows/release.yml`) works it out:
  this year, and one more than the highest N among the year's releases on GitHub, drafts included (a draft's tag
  exists only once it is published, so the list of releases is read, not the tags). It passes it to every build as
  `-DPRODUCT_VERSION=YEAR.N`. A version can still be given by hand when the workflow runs: to make a release again, or
  to replace the draft a failed run left (only a draft; BUILDING.md "Releases").
* **Development builds** report `cmake/Branding.cmake`'s default, `2026.0`: "release 0" of the year, older than every
  release, so the update check offers them the latest. Bump its year at the new year if you like; nothing depends on
  it. CMake refuses a `PRODUCT_VERSION` that is not YEAR.N.
* **Comparing** (`ui::compareVersions`, the update check) goes number by number, so 2026.10 is after 2026.9 and
  2027.1 after 2026.50.
* **Windows Installer** takes versions of at most 255.255.65535, so the `.msi` carries (YEAR - 2000).N.0 (2026.1 is
  26.1.0; `cmake/CPackOptions.cmake.in`); later years and releases still sort later, which upgrading needs. Its file
  name keeps YEAR.N. The macOS bundle, the `.deb`, the AppImage and the About box show YEAR.N.
