#!/usr/bin/env bash
set -euo pipefail

HOST="${1:-${CARDPUTER_ZERO_HOST:-}}"
SSH_USER="${2:-${CARDPUTER_ZERO_USER:-pi}}"
if [ -z "$HOST" ]; then
  echo "usage: $0 <host> [ssh-user]" >&2
  exit 2
fi

TARGET="${SSH_USER}@${HOST}"
REMOTE_DIR="/tmp/cardputerzero-orbit-deploy"
ABOUT_IMAGE="assets/orbit/fsm.jpg"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
export PATH="/opt/homebrew/bin:/usr/local/bin:${PATH}"

cd "$(dirname "$0")"
if command -v nc >/dev/null 2>&1 && ! nc -z -w 5 "$HOST" 22; then
  echo "cannot reach SSH at $HOST:22; check the device IP and Wi-Fi connection" >&2
  exit 1
fi

SCONS_BIN="${SCONS_BIN:-$(cd .. && pwd)/.venv312/bin/scons}"
if [ ! -x "$SCONS_BIN" ]; then
  SCONS_BIN="$(command -v scons || true)"
fi
if [ -z "$SCONS_BIN" ]; then
  echo "scons not found; activate ../.venv312 or set SCONS_BIN" >&2
  exit 1
fi

if [ "${SKIP_BUILD:-0}" != "1" ]; then
  CONFIG_REPO_AUTOMATION=1 CardputerZero=y "$SCONS_BIN" distclean
  CONFIG_REPO_AUTOMATION=1 CardputerZero=y "$SCONS_BIN" -j"${JOBS}"
fi
if ! file dist/M5CardputerZero-AstroBox | grep -q 'ELF 64-bit.*ARM aarch64'; then
  echo "device build failed: binary is not Linux AArch64" >&2
  file dist/M5CardputerZero-AstroBox >&2
  exit 1
fi

ssh -o StrictHostKeyChecking=accept-new "$TARGET" \
  "mkdir -p '$REMOTE_DIR/bin' '$REMOTE_DIR/applications' '$REMOTE_DIR/share/images' '$REMOTE_DIR/assets/orbit'"
rsync -az -e "ssh -o StrictHostKeyChecking=accept-new" \
  dist/M5CardputerZero-AstroBox "$TARGET:$REMOTE_DIR/bin/"
rsync -az -e "ssh -o StrictHostKeyChecking=accept-new" \
  applications/orbit.desktop "$TARGET:$REMOTE_DIR/applications/"
rsync -az -e "ssh -o StrictHostKeyChecking=accept-new" \
  share/images/astro-box.png "$TARGET:$REMOTE_DIR/share/images/"
if [ -f "$ABOUT_IMAGE" ]; then
  rsync -az -e "ssh -o StrictHostKeyChecking=accept-new" \
    "$ABOUT_IMAGE" "$TARGET:$REMOTE_DIR/assets/orbit/"
fi

ssh -tt -o StrictHostKeyChecking=accept-new "$TARGET" "set -e
sudo -v
sudo mkdir -p /usr/share/APPLaunch/bin /usr/share/APPLaunch/applications /usr/share/APPLaunch/share/images /usr/share/APPLaunch/assets/orbit
sudo install -m 0755 '$REMOTE_DIR/bin/M5CardputerZero-AstroBox' /usr/share/APPLaunch/bin/M5CardputerZero-AstroBox
sudo install -m 0644 '$REMOTE_DIR/applications/orbit.desktop' /usr/share/APPLaunch/applications/orbit.desktop
sudo install -m 0644 '$REMOTE_DIR/share/images/astro-box.png' /usr/share/APPLaunch/share/images/astro-box.png
if [ -f '$REMOTE_DIR/assets/orbit/fsm.jpg' ]; then
  sudo install -m 0644 '$REMOTE_DIR/assets/orbit/fsm.jpg' /usr/share/APPLaunch/assets/orbit/fsm.jpg
fi
restarted=0
for unit in APPLaunch.service applaunch.service M5CardputerZero-APPLaunch.service; do
  if sudo systemctl cat \"\$unit\" >/dev/null 2>&1; then
    sudo systemctl restart \"\$unit\"
    echo \"restarted \$unit\"
    restarted=1
    break
  fi
done
if [ \"\$restarted\" -eq 0 ]; then
  echo 'installed Astro Box; no APPLaunch systemd unit found. Restart APPLaunch or reboot the device to reload it.' >&2
fi"

echo "deployed Astro Box to $TARGET"
