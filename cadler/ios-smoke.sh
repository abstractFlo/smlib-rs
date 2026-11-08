#!/usr/bin/env bash
# The iOS libraries cadler/build-ios.sh wrote, put to work in the iOS simulator: the thread harness
# (cadler/threads/harness.cpp, every SM_API operation cadler uses, in 1 to N threads, each result
# compared bit for bit with the same run alone) built for the simulator against them and run there.
# The device libraries are built from the same sources with the same flags; only a device runs them.
#
#   cadler/ios-smoke.sh [threads] [passes]      after cadler/build-ios.sh, on macOS with Xcode
#
# Exit code 0 when every result matched in every thread.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THREADS="${1:-1,2,4}"
PASSES="${2:-1}"
MINIMUM="${IPHONEOS_DEPLOYMENT_TARGET:-16.0}"
LIBS="$ROOT/_build/ios/stage/aarch64-apple-ios-sim/lib"
OUT="$ROOT/_build/ios/smoke"
[ -f "$LIBS/libSMLib.a" ] || { echo "no simulator libraries in $LIBS: run cadler/build-ios.sh first"; exit 1; }
mkdir -p "$OUT"

echo "==> the thread harness for the simulator"
xcrun --sdk iphonesimulator clang++ -target "arm64-apple-ios$MINIMUM-simulator" -std=c++17 -O2 -DNDEBUG -DNOMINMAX \
  -DSM_GRAPHICS_CALLBACKS=1 -w -I"$ROOT/source/SM_API/inc" -I"$ROOT/source/SMLib/inc" \
  "$ROOT/cadler/threads/harness.cpp" -o "$OUT/harness" -L"$LIBS" -lSM_API -lSMLib

# a simulator to run it in: the first available iPad, else any iPhone
device="$(xcrun simctl list devices available -j | jq -r '[.devices | to_entries[] | select(.key | contains("iOS")) | .value[]] | sort_by(.name | contains("iPad") | not) | .[0].udid // empty')"
[ -n "$device" ] || { echo "no iOS simulator available"; xcrun simctl list devices; exit 1; }
echo "==> booting simulator $device"
xcrun simctl boot "$device" 2>/dev/null || true
xcrun simctl bootstatus "$device" -b
trap 'xcrun simctl shutdown "$device" >/dev/null 2>&1 || true' EXIT

echo "==> the harness in the simulator: threads $THREADS, $PASSES pass(es)"
xcrun simctl spawn "$device" "$OUT/harness" --threads "$THREADS" --passes "$PASSES"
