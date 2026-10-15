#!/bin/bash
# Keeps a copy of a package built by CI on a self-hosted runner, for the
# owner to try: the .dmg on the Mac, the AppImage on Linux. The runner's work
# folder is cleaned after each job, so the copy goes to the folder named by
# CUTTHULHU_BUILDS, which the runner setup scripts' "builds" command creates
# (a link to it is in the owner's Documents). Each copy is named with the
# date and commit; the newest five of each kind are kept.
#
#   scripts/keep-build.sh <package>...
#
# Without CUTTHULHU_BUILDS it does nothing.
set -euo pipefail

dir="${CUTTHULHU_BUILDS:-}"
if [ -z "$dir" ]; then
    echo "keep-build: CUTTHULHU_BUILDS is not set; nothing kept (see BUILDING.md)"
    exit 0
fi
if [ ! -d "$dir" ] || [ ! -w "$dir" ]; then
    echo "::warning::keep-build: $dir is missing or not writable; run the setup script's builds command"
    exit 0
fi

keep=5
sha=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
stamp=$(date +%Y-%m-%d_%H%M)
for f in "$@"; do
    [ -f "$f" ] || { echo "keep-build: no such file: $f" >&2; exit 1; }
    base=$(basename "$f")
    ext="${base##*.}"
    out="$dir/${base%.*}_${stamp}_${sha}.$ext"
    cp "$f" "$out"
    chmod a+r "$out"
    [ "$ext" = AppImage ] && chmod a+x "$out"
    echo "keep-build: $out"
    # Only the newest copies of this kind.
    find "$dir" -maxdepth 1 -type f -name "*.$ext" -print0 |
        xargs -0 ls -1t | tail -n +$((keep + 1)) | while IFS= read -r old; do rm -f "$old"; done
done
