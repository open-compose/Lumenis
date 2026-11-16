#!/usr/bin/env bash
# Builds the FFmpeg that release packages carry (Phase 12.2b; docs/spec/release-codecs.md, docs/EXTENSIONS.md "FFmpeg
# rules"): LGPL only (--disable-gpl --disable-nonfree), shared, with the encoders of the codecs awaiting legal review
# left out (FFmpeg's AAC, AC-3/E-AC-3, MPEG-4 Part 2, ProRes; libx264 and libx265 are GPL and absent anyway), and the
# operating system's codecs on: VideoToolbox and AudioToolbox on macOS, VA-API on Linux. The decoders of those codecs
# stay compiled in, because FFmpeg's hardware decoding goes through them; Cutthulhu's policy refuses their software
# decoding (media::codecAllowed).
#
#   scripts/build-release-ffmpeg.sh <prefix>
#
# The source is FFmpeg's own repository on GitHub at a pinned tag, checked against its commit. Optional libraries
# (BSD-licensed: dav1d and SVT-AV1 for AV1, Opus, OpenJPEG for JPEG 2000) are used when pkg-config finds them; install
# them first (the release workflow: Homebrew or apt). Configure FFmpeg's options here, nowhere else.
set -euo pipefail

prefix="${1:?usage: build-release-ffmpeg.sh <prefix>}"
# The FFmpeg CI's macOS jobs test against (Homebrew's; BUILDING.md "The first release run failed": 7.1.1 decoded
# VideoToolbox ProRes washed out, and did not build with SVT-AV1 3.x). Move it with Homebrew's.
tag="n9.0.2"
commit="946fcce07b6dcd0331c8cc609192aeff5e1924f8" # git ls-remote https://github.com/FFmpeg/FFmpeg refs/tags/n9.0.2^{}
work="${TMPDIR:-/tmp}/cutthulhu-ffmpeg-$tag"

if [[ ! -d "$work/.git" ]]; then
    rm -rf "$work"
    git clone --quiet --depth 1 --branch "$tag" https://github.com/FFmpeg/FFmpeg.git "$work"
fi
got="$(git -C "$work" rev-parse HEAD)"
if [[ "$got" != "$commit" ]]; then
    echo "build-release-ffmpeg: $tag is $got, not the pinned $commit; check the tag before using it" >&2
    exit 1
fi

# The encoders of the codecs awaiting review (media::isPatentCodec), FFmpeg's own implementations.
disabled_encoders="aac,ac3,ac3_fixed,eac3,mpeg4,msmpeg4v2,msmpeg4v3,prores,prores_aw,prores_ks"

options=(
    --prefix="$prefix"
    --enable-shared --disable-static
    --disable-gpl --disable-nonfree
    --disable-programs --disable-doc --disable-debug
    --disable-encoder="$disabled_encoders"
)
case "$(uname -s)" in
    Darwin) options+=(--enable-videotoolbox --enable-audiotoolbox) ;;
    Linux) options+=(--enable-vaapi) ;;
esac
have() { pkg-config --exists "$1" 2>/dev/null; }
have dav1d && options+=(--enable-libdav1d)
have SvtAv1Enc && options+=(--enable-libsvtav1)
have opus && options+=(--enable-libopus)
have libopenjp2 && options+=(--enable-libopenjpeg)

cd "$work"
./configure "${options[@]}"
# A check that nothing GPL or awaiting review slipped through: FFmpeg's own configuration headers.
grep -q '^#define CONFIG_GPL 0' config.h || { echo "build-release-ffmpeg: configured with GPL code" >&2; exit 1; }
for e in aac ac3 eac3 mpeg4 prores prores_ks libx264 libx265; do
    upper="$(printf '%s' "$e" | tr '[:lower:]' '[:upper:]')" # macOS's bash 3.2 has no ${e^^}
    if grep -q "^#define CONFIG_${upper}_ENCODER 1" config_components.h; then
        echo "build-release-ffmpeg: the $e encoder is in" >&2
        exit 1
    fi
done
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
make -j"$jobs"
make install
echo "build-release-ffmpeg: FFmpeg $tag (LGPL) in $prefix"
