#!/bin/bash
# tools/cymbalis_identity/run.sh — the working tree's FastEngine against FastEngine at REF
# (default origin/main), in memory (check.cpp): first bit-identity of the output, with the
# default tuning and with the cascade and both dampings on; with --time, block times of both
# engines instead. The two engines and the checker are compiled into a temporary directory,
# with cymbalis_fast's flags, and linked with an existing build's libcymbalis_engine.a (the bake
# loader; the build must have the architecture asked for). Nothing else is written.
#
#   tools/cymbalis_identity/run.sh [--ref REF] [--build DIR] [--arch arm64|x86_64] [--time [--runs N]]
#
# On a Mac, --arch x86_64 checks the Intel slice (under Rosetta on Apple Silicon).
set -euo pipefail

REF=origin/main
BUILD=build
ARCH=""
TIME=()
while [ $# -gt 0 ]; do
  case "$1" in
    --ref) REF=$2; shift 2 ;;
    --build) BUILD=$2; shift 2 ;;
    --arch) ARCH=$2; shift 2 ;;
    --time) TIME+=(--time); shift ;;
    --runs) TIME+=(--runs "$2"); shift 2 ;;
    *) echo "usage: $0 [--ref REF] [--build DIR] [--arch arm64|x86_64] [--time [--runs N]]" >&2; exit 2 ;;
  esac
done

ROOT=$(git rev-parse --show-toplevel)
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/ref"
git -C "$ROOT" show "$REF:plugin/dsp/FastEngine.cpp" > "$TMP/ref/FastEngine.cpp"
git -C "$ROOT" show "$REF:plugin/dsp/FastEngine.h" > "$TMP/ref/FastEngine.h"

CXX=${CXX:-clang++}
FLAGS=(-std=gnu++17 -O3 -DNDEBUG -ffp-contract=fast -fno-fast-math -DCYMBALIS_TUNING=1 -I"$ROOT/plugin/dsp")
RUN=()
if [ -n "$ARCH" ]; then
  FLAGS+=(-arch "$ARCH")
  if [ "$ARCH" = x86_64 ]; then
    FLAGS+=(-msse4.1)
    if [ "$(uname -m)" = arm64 ]; then RUN=(arch -x86_64); fi
  fi
fi

# REF's engine with its class renamed; its own FastEngine.h is found next to it first.
"$CXX" "${FLAGS[@]}" -DFastEngine=FastEngineRef -c "$TMP/ref/FastEngine.cpp" -o "$TMP/ref.o" &
"$CXX" "${FLAGS[@]}" -c "$ROOT/plugin/dsp/FastEngine.cpp" -o "$TMP/new.o" &
"$CXX" "${FLAGS[@]}" -I"$TMP" -c "$ROOT/tools/cymbalis_identity/check.cpp" -o "$TMP/check.o" &
wait
"$CXX" ${ARCH:+-arch "$ARCH"} "$TMP/check.o" "$TMP/ref.o" "$TMP/new.o" "$BUILD/plugin/dsp/libcymbalis_engine.a" -o "$TMP/check"

BAKES=("$ROOT"/resources/presets/*.bake.json)
echo "FastEngine of the working tree against $REF ($(git -C "$ROOT" rev-parse --short "$REF"))${ARCH:+, $ARCH}"
# ${a[@]+"${a[@]}"}: an empty array under set -u in bash 3.2 (macOS).
if [ ${#TIME[@]} -gt 0 ]; then
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" "${TIME[@]}" "${BAKES[@]}"
else
  STATUS=0
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" "${BAKES[@]}" || STATUS=1
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" --mechanisms "${BAKES[@]}" || STATUS=1
  exit $STATUS
fi
