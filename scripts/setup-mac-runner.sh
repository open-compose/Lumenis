#!/bin/bash
# Sets up a self-hosted GitHub Actions runner for Cutthulhu's CI on an Apple
# Silicon Mac. It runs headless, as its own hidden standard user, started by
# launchd at boot: nobody logs in as that user, it never takes the screen,
# keyboard or mouse, and it keeps running while you work in your own account.
# Builds run at background priority so your own work comes first.
#
# Run from an administrator account (see BUILDING.md, "Self-hosted macOS
# runner"):
#
#   sudo scripts/setup-mac-runner.sh install <registration-token>
#   sudo scripts/setup-mac-runner.sh status
#   sudo scripts/setup-mac-runner.sh stop | start
#   sudo scripts/setup-mac-runner.sh remove <removal-token>
#   sudo scripts/setup-mac-runner.sh builds
#
# builds: CI then keeps each .dmg it builds in /Users/Shared/Cutthulhu
# Builds, linked from your Documents folder (the newest five).
#
# The token comes from GitHub: the repository's Settings > Actions > Runners >
# New self-hosted runner (install), or the runner's page there (remove). It is
# valid for one hour.
#
# Environment: RUNNER_USER (default ghrunner), RUNNER_JOBS (parallel build
# jobs, default 4), REPO_URL (default the Lumenis repository).
set -euo pipefail

RUNNER_USER="${RUNNER_USER:-ghrunner}"
RUNNER_JOBS="${RUNNER_JOBS:-4}"
REPO_URL="${REPO_URL:-https://github.com/open-compose/Lumenis}"
LABEL="com.open-compose.actions-runner"
PLIST="/Library/LaunchDaemons/$LABEL.plist"
HOME_DIR="/Users/$RUNNER_USER"
RUNNER_DIR="$HOME_DIR/actions-runner"

BUILDS_DIR="/Users/Shared/Cutthulhu Builds"

die() { echo "error: $*" >&2; exit 1; }
as_runner() { sudo -u "$RUNNER_USER" -H "$@"; }

[ "$(uname -s)" = Darwin ] || die "this script is for macOS"
[ "$(uname -m)" = arm64 ] || die "this script is for Apple Silicon (arm64)"
[ "$(id -u)" -eq 0 ] || die "run it with sudo from an administrator account"

cmd="${1:-}"
case "$cmd" in
install)
    token="${2:-}"
    [ -n "$token" ] || die "usage: sudo $0 install <registration-token>"

    # Tools the CI job needs, installed by an administrator for all users.
    xcode-select -p >/dev/null 2>&1 || die "install Xcode or the Command Line Tools first (xcode-select --install)"
    for tool in cmake ninja; do
        [ -x "/opt/homebrew/bin/$tool" ] || die "install $tool first: brew install cmake ninja"
    done
    [ -f /opt/homebrew/include/libavcodec/avcodec.h ] || die "install FFmpeg first: brew install ffmpeg pkg-config"

    # A hidden standard (not administrator) user. Its password is random and
    # never used: nobody logs in as it.
    if ! id "$RUNNER_USER" >/dev/null 2>&1; then
        echo "Creating the hidden standard user $RUNNER_USER"
        sysadminctl -addUser "$RUNNER_USER" -fullName "GitHub Actions runner" \
            -password "$(openssl rand -base64 24)" -home "$HOME_DIR" >/dev/null 2>&1
        createhomedir -c -u "$RUNNER_USER" >/dev/null
        dscl . create "/Users/$RUNNER_USER" IsHidden 1
    fi
    id "$RUNNER_USER" >/dev/null 2>&1 || die "could not create the user $RUNNER_USER"
    chmod 700 "$HOME_DIR"

    # The Python packages for the interchange checks, in a virtual environment
    # in the runner's home (BUILDING.md, "Python packages for the checks"); CI
    # keeps it current with the same script. Homebrew's Python is preferred:
    # OpenTimelineIO has wheels for it, while the Command Line Tools' Python
    # 3.9 has to compile it.
    python=/opt/homebrew/bin/python3
    [ -x "$python" ] || python=/usr/bin/python3
    script_dir=$(cd "$(dirname "$0")" && pwd)
    "$python" "$script_dir/check-python.py" --venv "$HOME_DIR/.cutthulhu-venv"
    chown -R "$RUNNER_USER" "$HOME_DIR/.cutthulhu-venv"

    # The runner itself, the latest release.
    if [ ! -x "$RUNNER_DIR/config.sh" ]; then
        url=$(curl -fsSL https://api.github.com/repos/actions/runner/releases/latest |
            grep -o '"browser_download_url": *"[^"]*actions-runner-osx-arm64-[0-9.]*\.tar\.gz"' |
            head -1 | sed 's/.*"\(https[^"]*\)"/\1/')
        [ -n "$url" ] || die "could not find the latest runner release"
        echo "Downloading $url"
        as_runner mkdir -p "$RUNNER_DIR"
        as_runner bash -c "cd '$RUNNER_DIR' && curl -fsSL '$url' | tar xz"
    fi
    echo "Registering the runner with $REPO_URL"
    as_runner bash -c "cd '$RUNNER_DIR' && ./config.sh --unattended --replace --url '$REPO_URL' --token '$token' \
        --name '$(scutil --get LocalHostName)-cutthulhu' --labels cutthulhu --work _work"

    # The runner's environment: Homebrew tools first, a limited number of
    # parallel compile jobs, and tools such as setup-python installed in the
    # runner's folder (they otherwise use /Users/runner, as on GitHub's Macs).
    as_runner bash -c "cat > '$RUNNER_DIR/.env'" <<EOF
LANG=en_US.UTF-8
PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
CMAKE_BUILD_PARALLEL_LEVEL=$RUNNER_JOBS
AGENT_TOOLSDIRECTORY=$RUNNER_DIR/_work/_tool
EOF

    # A launchd daemon (not an agent): it starts at boot without anyone
    # logging in, has no window server session, and runs in the background
    # QoS class at low priority.
    cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>UserName</key><string>$RUNNER_USER</string>
  <key>WorkingDirectory</key><string>$RUNNER_DIR</string>
  <key>ProgramArguments</key>
  <array><string>$RUNNER_DIR/run.sh</string></array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>HOME</key><string>$HOME_DIR</string>
    <key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>
  </dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>30</integer>
  <key>ProcessType</key><string>Background</string>
  <key>Nice</key><integer>10</integer>
  <key>LowPriorityIO</key><true/>
  <key>StandardOutPath</key><string>$HOME_DIR/runner.log</string>
  <key>StandardErrorPath</key><string>$HOME_DIR/runner.log</string>
</dict>
</plist>
EOF
    chown root:wheel "$PLIST"
    chmod 644 "$PLIST"
    launchctl bootout system "$PLIST" >/dev/null 2>&1 || true
    launchctl bootstrap system "$PLIST"
    echo
    echo "The runner is installed and started. Next:"
    echo "  1. Check it shows as Idle under the repository's Settings > Actions > Runners."
    echo "  2. Add the repository variable MAC_RUNNER = on (Settings > Secrets and variables >"
    echo "     Actions > Variables) so CI sends macOS jobs to it."
    echo "Log: $HOME_DIR/runner.log"
    ;;
status)
    launchctl print "system/$LABEL" 2>/dev/null | grep -E '^\s*(state|pid|last exit code)' || echo "not loaded"
    [ -f "$HOME_DIR/runner.log" ] && tail -5 "$HOME_DIR/runner.log"
    ;;
stop)
    # Until the next start or reboot; jobs wait in GitHub's queue.
    launchctl bootout system "$PLIST" || true
    echo "Stopped. While it is stopped, set MAC_RUNNER to off so CI does not wait for it."
    ;;
start)
    launchctl bootstrap system "$PLIST"
    echo "Started."
    ;;
builds)
    # Run with sudo from your own account: the link goes in your Documents.
    owner="${SUDO_USER:-}"
    [ -n "$owner" ] && [ "$owner" != root ] || die "run it with sudo from your own account"
    [ -x "$RUNNER_DIR/config.sh" ] || die "install the runner first"
    # In /Users/Shared: the runner's user cannot write in your home folder.
    mkdir -p "$BUILDS_DIR"
    chown "$RUNNER_USER" "$BUILDS_DIR"
    chmod 755 "$BUILDS_DIR"
    link="/Users/$owner/Documents/Cutthulhu Builds"
    if [ ! -e "$link" ]; then
        ln -s "$BUILDS_DIR" "$link"
        chown -h "$owner" "$link"
    fi
    as_runner bash -c "grep -v '^CUTTHULHU_BUILDS=' '$RUNNER_DIR/.env' > '$RUNNER_DIR/.env.new' || true
        echo 'CUTTHULHU_BUILDS=$BUILDS_DIR' >> '$RUNNER_DIR/.env.new'
        mv '$RUNNER_DIR/.env.new' '$RUNNER_DIR/.env'"
    # The runner reads .env when it starts (a job running now is cut short).
    launchctl kickstart -k "system/$LABEL"
    echo "CI keeps each .dmg in $BUILDS_DIR (newest five), linked from $link."
    ;;
remove)
    token="${2:-}"
    [ -n "$token" ] || die "usage: sudo $0 remove <removal-token>"
    launchctl bootout system "$PLIST" >/dev/null 2>&1 || true
    rm -f "$PLIST"
    as_runner bash -c "cd '$RUNNER_DIR' && ./config.sh remove --token '$token'" || true
    echo "Removed the service and unregistered the runner."
    echo "To delete the user and its files too: sudo sysadminctl -deleteUser $RUNNER_USER"
    ;;
*)
    sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
