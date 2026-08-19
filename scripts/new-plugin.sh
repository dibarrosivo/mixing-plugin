#!/usr/bin/env bash
#
# Scaffold a new plugin from templates/plugin/.
#
#   scripts/new-plugin.sh --dir voice-chain \
#                         --target VoiceChain \
#                         --name "Voice Chain" \
#                         --code Vch1 \
#                         --description "All-in-one vocal processor"
#
# Creates plugins/<dir>/ with a buildable gain-in/gain-out plugin wired to
# audio::dsp. No edit to the top-level CMakeLists is needed — plugins are
# auto-discovered.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEMPLATE="$REPO_ROOT/templates/plugin"

DIR="" TARGET="" NAME="" CODE="" DESCRIPTION=""

usage() {
    sed -n '3,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-1}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir)         DIR="$2";         shift 2 ;;
        --target)      TARGET="$2";      shift 2 ;;
        --name)        NAME="$2";        shift 2 ;;
        --code)        CODE="$2";        shift 2 ;;
        --description) DESCRIPTION="$2"; shift 2 ;;
        -h|--help)     usage 0 ;;
        *) echo "error: unknown argument '$1'" >&2; usage ;;
    esac
done

for required in DIR TARGET NAME CODE; do
    if [[ -z "${!required}" ]]; then
        echo "error: --${required,,} is required" >&2
        usage
    fi
done

: "${DESCRIPTION:=$NAME}"

# ── Validate the plugin code ────────────────────────────────────────────────
# Getting this wrong is not a build error, it is a host silently loading the
# wrong plugin. Catch it here as well as in CMake.
if [[ ! "$CODE" =~ ^[A-Za-z0-9]{4}$ ]]; then
    echo "error: --code must be exactly 4 alphanumeric characters (got '$CODE')" >&2
    exit 1
fi

if [[ ! "$CODE" =~ [A-Z] ]]; then
    echo "error: --code needs at least one uppercase letter (got '$CODE')" >&2
    exit 1
fi

if grep -rq "PLUGIN_CODE  *$CODE\b" "$REPO_ROOT/plugins" 2>/dev/null; then
    echo "error: plugin code '$CODE' is already used by another plugin:" >&2
    grep -rn "PLUGIN_CODE  *$CODE\b" "$REPO_ROOT/plugins" >&2
    exit 1
fi

if [[ ! "$TARGET" =~ ^[A-Za-z][A-Za-z0-9_]*$ ]]; then
    echo "error: --target must be a valid C++ identifier (got '$TARGET')" >&2
    exit 1
fi

DEST="$REPO_ROOT/plugins/$DIR"

if [[ -e "$DEST" ]]; then
    echo "error: $DEST already exists" >&2
    exit 1
fi

# ── Instantiate ─────────────────────────────────────────────────────────────
mkdir -p "$DEST/source"

substitute() {
    sed -e "s|@TARGET@|$TARGET|g" \
        -e "s|@CLASS@|$TARGET|g" \
        -e "s|@PRODUCT_NAME@|$NAME|g" \
        -e "s|@PLUGIN_CODE@|$CODE|g" \
        -e "s|@DESCRIPTION@|$DESCRIPTION|g" \
        "$1"
}

substitute "$TEMPLATE/CMakeLists.txt.in" > "$DEST/CMakeLists.txt"

for f in "$TEMPLATE"/source/*.in; do
    base="$(basename "$f" .in)"
    substitute "$f" > "$DEST/source/$base"
done

cat <<EOF

Created plugins/$DIR

  target       $TARGET
  product name $NAME
  plugin code  $CODE

Build it:

  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target ${TARGET}_All

Note the _All suffix. The bare '$TARGET' target is JUCE's shared-code
library and produces no loadable plugin on its own; '${TARGET}_All' builds
every declared format.

The VST3 is copied to ~/.vst3, and the standalone lands in
build/plugins/$DIR/${TARGET}_artefacts/Release/Standalone/.

EOF
