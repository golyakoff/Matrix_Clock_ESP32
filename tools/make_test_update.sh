#!/usr/bin/env bash
# Builds a test update for the update from the internet without publishing a release. Debug builds
# read their manifest from the "ota-test" pre-release (CONFIG_MC_UPDATE_MANIFEST_URL, see
# variants/debug_*.defaults):
#
#   tools/make_test_update.sh debug_16mb v2.0.1
#
# builds the variant as the given version into build/<variant>_test and writes to release_test/:
#   tetris-clock_<version>_<variant>_fw.bin
#   manifest.json
# Upload both to a GitHub pre-release tagged "ota-test" (replacing the old files): a clock running
# a lower version of the same debug variant then finds the update. Pre-releases are never "latest",
# so release builds don't see it.
#
# idf.py does not run from MSYS/Git Bash: on Windows run the idf.py command below in the ESP-IDF
# PowerShell, then this script with SKIP_BUILD=1.
set -euo pipefail
cd "$(dirname "$0")/.."

VARIANT="${1:?usage: $0 <debug variant> <version>}"
VERSION="${2:?usage: $0 <debug variant> <version>}"
PYTHON="${PYTHON:-python}"
OUT="release_test"
TAG="ota-test"
[ -f "variants/$VARIANT.defaults" ] || { echo "unknown variant: $VARIANT (see variants/)" >&2; exit 1; }
grep -q "/$TAG/manifest.json" "variants/$VARIANT.defaults" \
    || { echo "$VARIANT does not read the $TAG manifest" >&2; exit 1; }

[ -n "${SKIP_BUILD:-}" ] || idf.py -B "build/${VARIANT}_test" \
    -DSDKCONFIG="build/${VARIANT}_test/sdkconfig" \
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;variants/$VARIANT.defaults" \
    -DMC_VERSION="$VERSION" \
    build

mkdir -p "$OUT"
FW="$OUT/tetris-clock_${VERSION}_${VARIANT}_fw.bin"
cp "build/${VARIANT}_test/tetris-clock.bin" "$FW"
"$PYTHON" tools/make_manifest.py --version "$VERSION" \
    --base-url "https://github.com/golyakoff/Matrix_Clock_ESP32/releases/download/$TAG" \
    "$VARIANT=$FW" > "$OUT/manifest.json"
echo "Upload to the $TAG pre-release:"
ls -l "$FW" "$OUT/manifest.json"
