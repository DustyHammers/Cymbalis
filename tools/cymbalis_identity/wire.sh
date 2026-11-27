#!/bin/bash
# tools/cymbalis_identity/wire.sh — the wireframe cymbal's colours from the CPU path (wire::bronze,
# segmentColour, surfaceColour) of the working tree against REF (default origin/main), in memory
# (wire.cpp): every shape at rest and after hits, and bronze() over a grid of its inputs. Every shape
# but NOVA RIDE bit for bit; NOVA RIDE (CymbalShape::roseK 0.62) differs, and the two rose ends
# bronze() gives are checked (NOVA RIDE #452E24 and #8A6D5D; the others #704A3A and #DEB096).
# REF's and the working tree's plugin/ui model are each compiled with wire.cpp into a temporary
# directory, with the plugin's optimisation (-O3); nothing else is written.
#
#   tools/cymbalis_identity/wire.sh [--ref REF] [--arch arm64|x86_64] [--flags "EXTRA FLAGS"]
#
# --flags: more compiler flags for both, e.g. "-march=haswell" (fused multiply-add on x86, as on
# arm64) or "-ffp-contract=fast".
set -euo pipefail

REF=origin/main
ARCH=""
EXTRA=""
while [ $# -gt 0 ]; do
  case "$1" in
    --ref) REF=$2; shift 2 ;;
    --arch) ARCH=$2; shift 2 ;;
    --flags) EXTRA=$2; shift 2 ;;
    *) echo "usage: $0 [--ref REF] [--arch arm64|x86_64] [--flags \"EXTRA FLAGS\"]" >&2; exit 2 ;;
  esac
done

ROOT=$(git rev-parse --show-toplevel)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/ref/ui" "$TMP/ref/out" "$TMP/new/out"
for F in CymbalModel.h CymbalModel.cpp CymbalShapes.h; do
  git -C "$ROOT" show "$REF:plugin/ui/$F" > "$TMP/ref/ui/$F"
done

CXX=${CXX:-clang++}
# shellcheck disable=SC2206
FLAGS=(-std=gnu++17 -O3 -DNDEBUG $EXTRA)
RUN=()
if [ -n "$ARCH" ]; then
  FLAGS+=(-arch "$ARCH")
  if [ "$ARCH" = x86_64 ] && [ "$(uname -m)" = arm64 ]; then RUN=(arch -x86_64); fi
fi

"$CXX" "${FLAGS[@]}" -I"$TMP/ref" "$TMP/ref/ui/CymbalModel.cpp" "$ROOT/tools/cymbalis_identity/wire.cpp" -o "$TMP/ref/wire" &
"$CXX" "${FLAGS[@]}" -I"$ROOT/plugin" "$ROOT/plugin/ui/CymbalModel.cpp" "$ROOT/tools/cymbalis_identity/wire.cpp" -o "$TMP/new/wire" &
wait

echo "Wireframe colours of the working tree against $REF ($(git -C "$ROOT" rev-parse --short "$REF"))${ARCH:+, $ARCH}${EXTRA:+, $EXTRA}"
${RUN[@]+"${RUN[@]}"} "$TMP/ref/wire" dump "$TMP/ref/out"
${RUN[@]+"${RUN[@]}"} "$TMP/new/wire" dump "$TMP/new/out"
${RUN[@]+"${RUN[@]}"} "$TMP/new/wire" compare "$TMP/ref/out" "$TMP/new/out"
