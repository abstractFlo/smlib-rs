#!/usr/bin/env bash
# SMLib in several threads, checked (release on Linux and macOS, tsan and globals on Linux):
#
#   tools/threads/run.sh release [threads] [passes]   harness against the library smlib-sys builds:
#                                                     results bit for bit and the speedup
#   tools/threads/run.sh tsan [threads] [passes]      SMLib, SM_API and the harness built from source
#                                                     under ThreadSanitizer: fails on any data race
#                                                     (needs only g++)
#   tools/threads/run.sh globals                      writable globals of that library against
#                                                     allowed.txt (writable_globals.py)
#
# release and globals build smlib-sys first (cargo build --release). Windows: run-windows.ps1.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/tools/threads"
OUT="$ROOT/target/threads"
if [ "$(uname)" = Darwin ]; then
  CXX="${CXX:-c++}"
  JOBS="$(sysctl -n hw.ncpu)"
else
  CXX="${CXX:-g++}"
  JOBS="$(nproc)"
fi
MODE="${1:-release}"
THREADS="${2:-1,2,4,8,16}"
PASSES="${3:-3}"
# as smlib-sys builds them; the first include dir decides which StdAfx.h a source gets
COMMON=(-std=c++17 -DNDEBUG -DNOMINMAX -DSM_GRAPHICS_CALLBACKS=1 -DSM_API_EXPORTS -w)
FLAGS=("${COMMON[@]}" -I"$ROOT/source/SM_API/inc" -I"$ROOT/source/SMLib/inc")
SMLIB_FLAGS=("${COMMON[@]}" -I"$ROOT/source/SMLib/inc")  # TBB only with SM_USE_TBB, which nothing sets
SM_API_FLAGS=("${COMMON[@]}" -I"$ROOT/source/SM_API/inc" -I"$ROOT/source/SMLib/inc")
mkdir -p "$OUT"

built_library() {  # the newest libsmlib.a of a release build of smlib-sys
  cargo build --quiet --release -p smlib-sys --manifest-path "$ROOT/Cargo.toml"
  find "$ROOT/target/release/build" -name libsmlib.a -print0 | xargs -0 ls -t | head -1
}

case "$MODE" in
  release)
    LIBRARY="$(built_library)"
    "$CXX" -O2 -g "${FLAGS[@]}" "$HERE/harness.cpp" "$LIBRARY" -o "$OUT/harness" -lpthread
    "$OUT/harness" --threads "$THREADS" --passes "$PASSES"
    ;;
  tsan)
    # every source of SMLib and SM_API instrumented, linked into one program: races inside SMLib
    # are only seen when SMLib itself is built with -fsanitize=thread
    OBJECTS="$OUT/tsan-objects"
    mkdir -p "$OBJECTS"
    compile() {  # library, its flags...: every source of source/<library>/src, changed ones only
      local library="$1"
      shift
      printf '%s\n' "$ROOT/source/$library/src/"*.cpp | grep -vE '/(SmDllMain|StdAfx)\.cpp$' | xargs -P "$JOBS" -I{} sh -c '
        object="$0/$1-$(basename "{}" .cpp).o"
        shift
        [ "$object" -nt "{}" ] || g++ -O1 -g -fsanitize=thread -fPIC "$@" -c "{}" -o "$object"
      ' "$OBJECTS" "$library" "$@"
    }
    compile SMLib "${SMLIB_FLAGS[@]}"
    compile SM_API "${SM_API_FLAGS[@]}"
    g++ -O1 -g -fsanitize=thread "${FLAGS[@]}" "$HERE/harness.cpp" "$OBJECTS"/*.o -o "$OUT/harness-tsan" -lpthread
    # exit code 66 on any report, after the whole run (every race is listed, not only the first)
    TSAN_OPTIONS="halt_on_error=0 exitcode=66 report_signal_unsafe=0 history_size=4 ${TSAN_OPTIONS:-}" \
      "$OUT/harness-tsan" --threads "${2:-2,8}" --passes "${3:-1}"
    ;;
  globals)
    python3 "$HERE/writable_globals.py" "$(built_library)"
    ;;
  *)
    echo "usage: $0 release|tsan|globals [threads] [passes]" >&2
    exit 2
    ;;
esac
