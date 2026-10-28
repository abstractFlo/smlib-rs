#!/usr/bin/env bash
# SMLib in several threads, checked (release on Linux and macOS, tsan and globals on Linux):
#
#   cadler/threads/run.sh release [threads] [passes]   harness against the built release libraries:
#                                                      results bit for bit and the speedup
#   cadler/threads/run.sh tsan [threads] [passes]      SMLib, SM_API and the harness built from source
#                                                      under ThreadSanitizer: fails on any data race
#                                                      (needs only g++, no prebuild)
#   cadler/threads/run.sh globals                      writable globals of the built libraries
#                                                      against allowed.txt (writable_globals.py)
#
# release and globals need a release build in _build (cadler/build-linux.sh, build-macos.sh or
# repo.sh build -r). Windows: run-windows.ps1.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/cadler/threads"
OUT="$ROOT/_build/threads"
if [ "$(uname)" = Darwin ]; then
  LIBS="$ROOT/_build/macos-universal/release"
  CXX="${CXX:-c++}"
else
  LIBS="$ROOT/_build/linux-$(uname -m)/release"
  CXX="${CXX:-g++}"
fi
MODE="${1:-release}"
THREADS="${2:-1,2,4,8,16}"
PASSES="${3:-3}"
# as premake builds them (compile_commands.json); the first include dir decides which StdAfx.h a source gets
COMMON=(-std=c++17 -DNDEBUG -DNOMINMAX -DSM_GRAPHICS_CALLBACKS=1 -w)
FLAGS=("${COMMON[@]}" -I"$ROOT/source/SM_API/inc" -I"$ROOT/source/SMLib/inc")
SMLIB_FLAGS=("${COMMON[@]}" -I"$ROOT/source/SMLib/inc")  # TBB only with SM_USE_TBB, which nothing sets
SM_API_FLAGS=("${COMMON[@]}" -DSM_API_EXPORTS -I"$ROOT/source/SM_API/inc" -I"$ROOT/source/SMLib/inc")
mkdir -p "$OUT"

case "$MODE" in
  release)
    "$CXX" -O2 -g "${FLAGS[@]}" "$HERE/harness.cpp" -o "$OUT/harness" -L"$LIBS" -lSM_API -lSMLib -lpthread -Wl,-rpath,"$LIBS"
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
      printf '%s\n' "$ROOT/source/$library/src/"*.cpp | xargs -P "$(nproc)" -I{} sh -c '
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
    python3 "$HERE/writable_globals.py" "$LIBS"
    ;;
  *)
    echo "usage: $0 release|tsan|globals [threads] [passes]" >&2
    exit 2
    ;;
esac
