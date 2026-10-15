#!/bin/bash
# Sets up a self-hosted GitHub Actions runner for Cutthulhu's Linux CI inside a
# WSL Ubuntu distribution (or any Ubuntu with systemd). It runs as its own
# user, as a systemd service at low priority. One runner per distribution:
# its label is ubuntu-<version>, e.g. ubuntu-26.04.
#
# Run inside the distribution, from the checkout (see BUILDING.md,
# "Self-hosted Windows and Linux (WSL) runners"):
#
#   sudo scripts/setup-wsl-runner.sh install <registration-token>
#   sudo scripts/setup-wsl-runner.sh status
#   sudo scripts/setup-wsl-runner.sh stop | start
#   sudo scripts/setup-wsl-runner.sh remove <removal-token>
#
# On WSL the distribution must keep running: on Windows, run
#   scripts\setup-windows-runner.ps1 keep-wsl <distribution>
#
# Environment: RUNNER_USER (default ghrunner), RUNNER_JOBS (parallel build
# jobs, default 4), REPO_URL (default the Lumenis repository).
set -euo pipefail

RUNNER_USER="${RUNNER_USER:-ghrunner}"
RUNNER_JOBS="${RUNNER_JOBS:-4}"
REPO_URL="${REPO_URL:-https://github.com/open-compose/Lumenis}"
HOME_DIR="/home/$RUNNER_USER"
RUNNER_DIR="$HOME_DIR/actions-runner"

die() { echo "error: $*" >&2; exit 1; }
as_runner() { sudo -u "$RUNNER_USER" -H "$@"; }
service_name() { [ -f "$RUNNER_DIR/.service" ] && cat "$RUNNER_DIR/.service"; }

[ "$(uname -s)" = Linux ] || die "this script is for Linux (inside WSL)"
[ "$(uname -m)" = x86_64 ] || die "this script is for x86-64"
[ "$(id -u)" -eq 0 ] || die "run it with sudo"
# shellcheck disable=SC1091
. /etc/os-release
[ "${ID:-}" = ubuntu ] || die "this script is for Ubuntu"

cmd="${1:-}"
case "$cmd" in
install)
    token="${2:-}"
    [ -n "$token" ] || die "usage: sudo $0 install <registration-token>"
    [ "$(ps -p 1 -o comm=)" = systemd ] ||
        die "systemd is not running. Add [boot] systemd=true to /etc/wsl.conf, run 'wsl --shutdown' in Windows, and try again"

    # Everything the CI jobs need, installed for all users: the build
    # dependencies, git, weston for the Wayland check, and Pillow for the
    # screenshot comparison.
    script_dir=$(cd "$(dirname "$0")" && pwd)
    "$script_dir/bootstrap-linux.sh"
    apt-get install -y --no-install-recommends git ca-certificates fonts-dejavu-core weston curl file python3-pil

    if ! id "$RUNNER_USER" >/dev/null 2>&1; then
        echo "Creating the user $RUNNER_USER"
        useradd --create-home --shell /bin/bash --comment "GitHub Actions runner" "$RUNNER_USER"
    fi
    chmod 700 "$HOME_DIR"

    if [ ! -x "$RUNNER_DIR/config.sh" ]; then
        url=$(curl -fsSL https://api.github.com/repos/actions/runner/releases/latest |
            grep -o '"browser_download_url": *"[^"]*actions-runner-linux-x64-[0-9.]*\.tar\.gz"' |
            head -1 | sed 's/.*"\(https[^"]*\)"/\1/')
        [ -n "$url" ] || die "could not find the latest runner release"
        echo "Downloading $url"
        as_runner mkdir -p "$RUNNER_DIR"
        as_runner bash -c "cd '$RUNNER_DIR' && curl -fsSL '$url' | tar xz"
        "$RUNNER_DIR/bin/installdependencies.sh"
    fi
    echo "Registering the runner with $REPO_URL"
    as_runner bash -c "cd '$RUNNER_DIR' && ./config.sh --unattended --replace --url '$REPO_URL' --token '$token' \
        --name '$(hostname)-cutthulhu-ubuntu-$VERSION_ID' --labels 'cutthulhu,ubuntu-$VERSION_ID' --work _work"
    as_runner bash -c "cat > '$RUNNER_DIR/.env'" <<EOF
LANG=C.UTF-8
CMAKE_BUILD_PARALLEL_LEVEL=$RUNNER_JOBS
EOF

    # A systemd service, at low CPU and I/O priority.
    (cd "$RUNNER_DIR" && ./svc.sh install "$RUNNER_USER")
    unit=$(service_name)
    mkdir -p "/etc/systemd/system/$unit.d"
    cat > "/etc/systemd/system/$unit.d/priority.conf" <<EOF
[Service]
Nice=10
CPUSchedulingPolicy=batch
IOSchedulingClass=idle
EOF
    systemctl daemon-reload
    (cd "$RUNNER_DIR" && ./svc.sh start)
    echo
    echo "The runner is installed and started (label ubuntu-$VERSION_ID). Next:"
    echo "  1. On Windows (elevated PowerShell): scripts\\setup-windows-runner.ps1 keep-wsl $WSL_DISTRO_NAME"
    echo "  2. Check it shows as Idle under the repository's Settings > Actions > Runners."
    echo "  3. Set the repository variable LINUX_RUNNERS to the versions you have runners for,"
    echo "     e.g. [\"$VERSION_ID\"] (Settings > Secrets and variables > Actions > Variables)."
    ;;
status)
    unit=$(service_name) || die "not installed"
    systemctl status "$unit" --no-pager | head -12
    ;;
stop)
    (cd "$RUNNER_DIR" && ./svc.sh stop)
    echo "Stopped. While it is stopped, take its version out of LINUX_RUNNERS so CI does not wait for it."
    ;;
start)
    (cd "$RUNNER_DIR" && ./svc.sh start)
    ;;
remove)
    token="${2:-}"
    [ -n "$token" ] || die "usage: sudo $0 remove <removal-token>"
    (cd "$RUNNER_DIR" && ./svc.sh stop && ./svc.sh uninstall) || true
    as_runner bash -c "cd '$RUNNER_DIR' && ./config.sh remove --token '$token'" || true
    echo "Removed the service and unregistered the runner."
    echo "To delete the user and its files too: sudo userdel -r $RUNNER_USER"
    ;;
*)
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
