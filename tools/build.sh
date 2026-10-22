#!/usr/bin/env bash
# Builds one variant (default release_4mb) into build/<variant>:
#
#   . ~/esp/esp-idf-v5.5.6/export.sh
#   tools/build.sh release_4mb [idf.py args, default "build"]
#
# Variants: variants/<variant>.defaults on top of sdkconfig.defaults.
set -euo pipefail
cd "$(dirname "$0")/.."

VARIANT="${1:-release_4mb}"
shift || true
[ -f "variants/$VARIANT.defaults" ] || { echo "unknown variant: $VARIANT (see variants/)" >&2; exit 1; }
[ $# -gt 0 ] || set -- build

idf.py -B "build/$VARIANT" \
    -DSDKCONFIG="build/$VARIANT/sdkconfig" \
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;variants/$VARIANT.defaults" \
    "$@"
