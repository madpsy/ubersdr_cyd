#!/usr/bin/env bash
# build.sh — Build the firmware + a clean LittleFS image and stage all the
# binaries the browser-based web flasher needs into docs/flash/.
#
# Usage:
#   ./build.sh
#
# The LittleFS image is built from a staging copy of data/ containing ONLY the
# .example files, so your local (gitignored) wifi.json / ubersdr.json
# credentials never end up in the published image. Commit the updated
# docs/flash/*.bin files and GitHub Pages (serving /docs) picks them up.
#
# Note: this leaves the clean LittleFS image in .pio/ — a subsequent
# ./deploy.sh fs rebuilds it from your real data/ automatically.

set -euo pipefail
cd "$(dirname "$0")"

ENV="esp32-2432s028r"
BUILD_DIR=".pio/build/$ENV"
OUT="docs/flash"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

step() { echo; echo "▶ $*"; }
ok()   { echo "✓ $*"; }
fail() { echo "✗ $*" >&2; exit 1; }

step "Building firmware..."
pio run -e "$ENV" || fail "Build failed"
ok "Build complete"

step "Building clean LittleFS image (example config only)..."
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp data/*.example "$STAGE"/
PLATFORMIO_DATA_DIR="$STAGE" pio run -e "$ENV" -t buildfs || fail "buildfs failed"
ok "LittleFS image built"

step "Staging web-flasher binaries in $OUT/ ..."
[[ -f "$BOOT_APP0" ]] || fail "boot_app0.bin not found at $BOOT_APP0"
mkdir -p "$OUT"
cp "$BUILD_DIR/bootloader.bin" "$OUT/bootloader.bin"
cp "$BUILD_DIR/partitions.bin" "$OUT/partitions.bin"
cp "$BOOT_APP0"                "$OUT/boot_app0.bin"
cp "$BUILD_DIR/firmware.bin"   "$OUT/firmware.bin"
cp "$BUILD_DIR/littlefs.bin"   "$OUT/littlefs.bin"
ok "Binaries staged"

echo
echo "✓ Done. Commit docs/flash/*.bin to publish via the web flasher."
