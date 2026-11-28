#!/bin/bash
# tools/cymbalis_identity/run.sh — the working tree's FastEngine against FastEngine at REF
# (default origin/main), in memory (check.cpp): first bit-identity of the output, with the
# default tuning and with the cascade and both dampings on; with --time, block times of both
# engines instead. The two engines and the checker are compiled into a temporary directory,
# with cymbalis_fast's flags, and linked with an existing build's libcymbalis_engine.a (the bake
# loader; the build must have the architecture asked for). Nothing else is written. With --bound,
# for a change meant to differ only far below hearing, the bounds of check.cpp's --bound instead
# of bit-identity (docs/PORT_MAP.md, "One silence rule: 140 dB below a full hit").
#
#   tools/cymbalis_identity/run.sh [--ref REF] [--build DIR] [--arch arm64|x86_64] [--time [--runs N] | --bound | --splash | --chain | --nova-edge-v2 | --edge-ring | --edge-click]
#
# --splash: the hi-hat's foot splash and fast-tap FOOT against REF (check.cpp's --splash).
# --chain: the sizzle chain's levels, order, stereo and switching, the working tree's engine
# alone (check.cpp's --chain), on the bakes with a chain block.
# --nova-edge-v2: NOVA RIDE edge v2 (check.cpp's --nova-edge-v2): REF's engine on REF's NOVA bake
# against the working tree's on its own (docs/PORT_MAP.md, "NOVA RIDE edge v2").
# --edge-ring: an envelope strike keeps its ring when another strike follows (check.cpp's
# --edge-ring): the edge ring figures of both engines, and what must stay bit for bit.
# --edge-click: an envelope strike's contact plays at its envelope level (check.cpp's
# --edge-click): the hand-over figures of both engines, and what must stay bit for bit.
#
# The sizzle chain (plugin/dsp/SizzleChain.cpp) is compiled with each engine that has it; REF's
# with its class renamed, as its FastEngine.
#
# A bake with added modes ("added_modes", plugin/dsp/Bake.h) whose copy at REF has none is left
# out of the identity sequences and named: REF's engine draws its strike phases as if it had none.
#
# On a Mac, --arch x86_64 checks the Intel slice (under Rosetta on Apple Silicon).
set -euo pipefail

REF=origin/main
BUILD=build
ARCH=""
TIME=()
BOUND=0
SPLASH=0
CHAIN=0
NOVA=0
EDGERING=0
EDGECLICK=0
while [ $# -gt 0 ]; do
  case "$1" in
    --ref) REF=$2; shift 2 ;;
    --build) BUILD=$2; shift 2 ;;
    --arch) ARCH=$2; shift 2 ;;
    --time) TIME+=(--time); shift ;;
    --runs) TIME+=(--runs "$2"); shift 2 ;;
    --bound) BOUND=1; shift ;;
    --splash) SPLASH=1; shift ;;
    --chain) CHAIN=1; shift ;;
    --nova-edge-v2) NOVA=1; shift ;;
    --edge-ring) EDGERING=1; shift ;;
    --edge-click) EDGECLICK=1; shift ;;
    *) echo "usage: $0 [--ref REF] [--build DIR] [--arch arm64|x86_64] [--time [--runs N] | --bound | --splash | --chain | --nova-edge-v2 | --edge-ring | --edge-click]" >&2; exit 2 ;;
  esac
done

ROOT=$(git rev-parse --show-toplevel)
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/ref"
git -C "$ROOT" show "$REF:plugin/dsp/FastEngine.cpp" > "$TMP/ref/FastEngine.cpp"
git -C "$ROOT" show "$REF:plugin/dsp/FastEngine.h" > "$TMP/ref/FastEngine.h"
REF_CHAIN=0
if git -C "$ROOT" cat-file -e "$REF:plugin/dsp/SizzleChain.cpp" 2>/dev/null; then
  git -C "$ROOT" show "$REF:plugin/dsp/SizzleChain.cpp" > "$TMP/ref/SizzleChain.cpp"
  git -C "$ROOT" show "$REF:plugin/dsp/SizzleChain.h" > "$TMP/ref/SizzleChain.h"
  REF_CHAIN=1
fi

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

# REF's engine with its class renamed; its own FastEngine.h (and SizzleChain.h) is found next to
# it first.
OBJS=("$TMP/check.o" "$TMP/ref.o" "$TMP/new.o")
"$CXX" "${FLAGS[@]}" -DFastEngine=FastEngineRef -DSizzleChain=SizzleChainRef -c "$TMP/ref/FastEngine.cpp" -o "$TMP/ref.o" &
"$CXX" "${FLAGS[@]}" -c "$ROOT/plugin/dsp/FastEngine.cpp" -o "$TMP/new.o" &
"$CXX" "${FLAGS[@]}" -I"$TMP" -c "$ROOT/tools/cymbalis_identity/check.cpp" -o "$TMP/check.o" &
if [ -f "$ROOT/plugin/dsp/SizzleChain.cpp" ]; then
  "$CXX" "${FLAGS[@]}" -c "$ROOT/plugin/dsp/SizzleChain.cpp" -o "$TMP/chain.o" &
  OBJS+=("$TMP/chain.o")
fi
if [ "$REF_CHAIN" = 1 ]; then
  "$CXX" "${FLAGS[@]}" -DSizzleChain=SizzleChainRef -c "$TMP/ref/SizzleChain.cpp" -o "$TMP/refchain.o" &
  OBJS+=("$TMP/refchain.o")
fi
wait
"$CXX" ${ARCH:+-arch "$ARCH"} "${OBJS[@]}" "$BUILD/plugin/dsp/libcymbalis_engine.a" -o "$TMP/check"

# Every bake REF's engine can play: a preset newer than REF (the hi-hat, with its own strike
# model, before it was added) is left out and named.
BAKES=()
for B in "$ROOT"/resources/presets/*.bake.json; do
  if ! git -C "$ROOT" cat-file -e "$REF:resources/presets/$(basename "$B")" 2>/dev/null; then
    echo "skipped $(basename "$B"): not in $REF"
    continue
  fi
  if grep -q '"added_modes"' "$B" && ! git -C "$ROOT" show "$REF:resources/presets/$(basename "$B")" | grep -q '"added_modes"'; then
    echo "skipped $(basename "$B"): it has added modes, its copy in $REF none (--nova-edge-v2 checks NOVA RIDE against $REF's bake)"
    continue
  fi
  BAKES+=("$B")
done
echo "FastEngine of the working tree against $REF ($(git -C "$ROOT" rev-parse --short "$REF"))${ARCH:+, $ARCH}"
# ${a[@]+"${a[@]}"}: an empty array under set -u in bash 3.2 (macOS).
if [ "$CHAIN" = 1 ]; then
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" --chain "${BAKES[@]}"
elif [ "$NOVA" = 1 ]; then
  git -C "$ROOT" show "$REF:resources/presets/nova_ride.bake.json" > "$TMP/nova_ride.ref.bake.json"
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" --nova-edge-v2 "$TMP/nova_ride.ref.bake.json" "$ROOT/resources/presets/nova_ride.bake.json"
elif [ "$EDGERING" = 1 ]; then
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" --edge-ring "${BAKES[@]}"
elif [ "$EDGECLICK" = 1 ]; then
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" --edge-click "${BAKES[@]}"
elif [ "$SPLASH" = 1 ]; then
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" --splash "${BAKES[@]}"
elif [ "$BOUND" = 1 ]; then
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" --bound "${BAKES[@]}"
elif [ ${#TIME[@]} -gt 0 ]; then
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" "${TIME[@]}" "${BAKES[@]}"
else
  STATUS=0
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" "${BAKES[@]}" || STATUS=1
  ${RUN[@]+"${RUN[@]}"} "$TMP/check" --mechanisms "${BAKES[@]}" || STATUS=1
  exit $STATUS
fi
