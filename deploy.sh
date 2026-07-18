#!/usr/bin/env bash
# deploy.sh — Build and flash the UberSDR CYD firmware + LittleFS filesystem.
#
# Usage:
#   ./deploy.sh           # build + upload firmware + upload filesystem
#   ./deploy.sh firmware  # build + upload firmware only
#   ./deploy.sh fs        # upload filesystem only (no build)
#   ./deploy.sh build     # build only (no upload)
#
# The upload port is taken from platformio.ini (upload_port = /dev/ttyUSB0).
# Override with:  UPLOAD_PORT=/dev/ttyUSB1 ./deploy.sh

set -euo pipefail

ENV="esp32-2432s028r"
PIO="pio"

# Allow port override via environment variable.
if [[ -n "${UPLOAD_PORT:-}" ]]; then
  PORT_FLAG="--upload-port ${UPLOAD_PORT}"
else
  PORT_FLAG=""
fi

step() { echo; echo "▶ $*"; }
ok()   { echo "✓ $*"; }
fail() { echo "✗ $*" >&2; exit 1; }

MODE="${1:-all}"

case "$MODE" in
  build)
    step "Building firmware..."
    $PIO run -e "$ENV" || fail "Build failed"
    ok "Build complete"
    ;;

  firmware)
    step "Building firmware..."
    $PIO run -e "$ENV" || fail "Build failed"
    ok "Build complete"

    step "Uploading firmware..."
    # shellcheck disable=SC2086
    $PIO run -e "$ENV" -t upload $PORT_FLAG || fail "Firmware upload failed"
    ok "Firmware uploaded"
    ;;

  fs)
    step "Uploading LittleFS filesystem (data/)..."
    # shellcheck disable=SC2086
    $PIO run -e "$ENV" -t uploadfs $PORT_FLAG || fail "Filesystem upload failed"
    ok "Filesystem uploaded"
    ;;

  all)
    step "Building firmware..."
    $PIO run -e "$ENV" || fail "Build failed"
    ok "Build complete"

    step "Uploading firmware..."
    # shellcheck disable=SC2086
    $PIO run -e "$ENV" -t upload $PORT_FLAG || fail "Firmware upload failed"
    ok "Firmware uploaded"

    step "Uploading LittleFS filesystem (data/)..."
    # shellcheck disable=SC2086
    $PIO run -e "$ENV" -t uploadfs $PORT_FLAG || fail "Filesystem upload failed"
    ok "Filesystem uploaded"

    echo
    echo "✓ Deploy complete — firmware + filesystem written to device."
    ;;

  *)
    echo "Usage: $0 [all|build|firmware|fs]"
    exit 1
    ;;
esac
