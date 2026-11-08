#!/usr/bin/env bash
# SMLib and SM_API for iOS as static libraries, for cadler's geometry kernel inside its iPad app
# (an iOS app may start no process beside it, so the kernel links them; cadler's docs/ipad-app.md).
# Every source of source/SMLib and source/SM_API is compiled with Xcode's clang as the Linux and
# macOS builds compile them (cadler/threads/run.sh), once for the device (arm64, iphoneos) and once
# for the simulator (arm64, iphonesimulator): no OpenUSD, no Python and no TBB (SMLib uses it only
# with SM_USE_TBB, which nothing sets).
#
#   cadler/build-ios.sh [out dir]     dist/ by default; writes usd-brep-<version>-ios.tar.gz and its .sha256:
#                                       aarch64-apple-ios/lib/libSMLib.a, libSM_API.a       the device
#                                       aarch64-apple-ios-sim/lib/libSMLib.a, libSM_API.a   the simulator
#                                     (the target names are Rust's, which cadler's kernel builds for)
#
# Needs Xcode (xcrun with the iphoneos and iphonesimulator SDKs). The objects stay in _build/ios/ and
# only changed sources are compiled again.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(cat "$ROOT/cadler/version")"
OUT="$(mkdir -p "${1:-$ROOT/dist}" && cd "${1:-$ROOT/dist}" && pwd)"
BUILD="$ROOT/_build/ios"
# iOS 16: cadler builds its kernel for it too (IPHONEOS_DEPLOYMENT_TARGET, its docs/ipad-app.md)
MINIMUM="${IPHONEOS_DEPLOYMENT_TARGET:-16.0}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
# release flags of the Linux and macOS builds (premake5.lua); warnings do not fail this build, the
# others with -Werror stand for the code
COMMON=(-std=c++17 -O2 -DNDEBUG -DNOMINMAX -DSM_GRAPHICS_CALLBACKS=1 -fexceptions -frtti -w)

[ "$(uname -s)" = Darwin ] || { echo "this builds on macOS with Xcode only"; exit 1; }
for sdk in iphoneos iphonesimulator; do
  xcrun --sdk "$sdk" --show-sdk-path >/dev/null || { echo "Xcode's $sdk SDK is missing"; exit 1; }
done

# sdk, clang target, Rust target
PLATFORMS=(
  "iphoneos arm64-apple-ios$MINIMUM aarch64-apple-ios"
  "iphonesimulator arm64-apple-ios$MINIMUM-simulator aarch64-apple-ios-sim"
)

compile() {  # sdk, clang target, objects folder, library, its flags...: every source, changed ones only
  local sdk="$1" target="$2" objects="$3" library="$4"
  shift 4
  mkdir -p "$objects"
  # macOS's xargs keeps a command with -I under 255 bytes: the flags go by the environment, a source
  # as the one argument (no flag or path here has a space in it)
  find "$ROOT/source/$library/src" -maxdepth 1 -name '*.cpp' -print0 \
    | COMPILE_SDK="$sdk" COMPILE_TARGET="$target" COMPILE_OBJECTS="$objects" COMPILE_FLAGS="$*" xargs -0 -n 1 -P "$JOBS" sh -c '
      object="$COMPILE_OBJECTS/$(basename "$1" .cpp).o"
      [ "$object" -nt "$1" ] || xcrun --sdk "$COMPILE_SDK" clang++ -target "$COMPILE_TARGET" $COMPILE_FLAGS -c "$1" -o "$object" \
        || { echo "failed: $1" >&2; exit 255; }
    ' sh
}

stage="$BUILD/stage"
rm -rf "$stage"
for platform in "${PLATFORMS[@]}"; do
  read -r sdk target rust_target <<< "$platform"
  echo "==> SMLib and SM_API for $rust_target ($target)"
  objects="$BUILD/$rust_target"
  compile "$sdk" "$target" "$objects/SMLib" SMLib "${COMMON[@]}" -I"$ROOT/source/SMLib/inc"
  compile "$sdk" "$target" "$objects/SM_API" SM_API "${COMMON[@]}" -DSM_API_EXPORTS -I"$ROOT/source/SM_API/inc" -I"$ROOT/source/SMLib/inc"
  mkdir -p "$stage/$rust_target/lib"
  for library in SMLib SM_API; do
    xcrun --sdk "$sdk" libtool -static -no_warning_for_no_symbols -o "$stage/$rust_target/lib/lib$library.a" "$objects/$library/"*.o
  done
  ls -la "$stage/$rust_target/lib"
done

cp "$ROOT/LICENSE" "$stage/LICENSE"
echo "$VERSION" > "$stage/VERSION"
archive="$OUT/usd-brep-$VERSION-ios.tar.gz"
tar -czf "$archive" -C "$stage" .
(cd "$OUT" && shasum -a 256 "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "$archive"
