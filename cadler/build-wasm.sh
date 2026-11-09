#!/usr/bin/env bash
# SMLib and SM_API as WebAssembly static libraries, for cadler's geometry kernel in a browser's Web
# Worker (cadler's docs/wasm-kernel.md). Every source of source/SMLib and source/SM_API is compiled
# with Emscripten's em++ as cadler/build-ios.sh compiles them for the iPad: no OpenUSD, no Python and
# no TBB (SMLib uses it only with SM_USE_TBB, which nothing sets). Exceptions are WebAssembly's own
# (-fwasm-exceptions), as Rust's Emscripten target unwinds with them.
#
#   cadler/build-wasm.sh [out dir]    dist/ by default; writes usd-brep-<version>-wasm.tar.gz and its .sha256:
#                                       wasm32-unknown-emscripten/lib/libSMLib.a, libSM_API.a   one thread
#                                     (the target name is Rust's, which cadler's kernel builds for)
#
# Needs Emscripten (em++ and emar on PATH, emsdk_env.sh). The objects stay in _build/wasm/ and only
# changed sources are compiled again.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(cat "$ROOT/cadler/version")"
OUT="$(mkdir -p "${1:-$ROOT/dist}" && cd "${1:-$ROOT/dist}" && pwd)"
BUILD="$ROOT/_build/wasm"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
RUST_TARGET=wasm32-unknown-emscripten
# release flags of the other builds (premake5.lua, build-ios.sh); warnings do not fail this build
COMMON=(-std=c++17 -O2 -DNDEBUG -DNOMINMAX -DSM_GRAPHICS_CALLBACKS=1 -fwasm-exceptions -frtti -w)

command -v em++ >/dev/null && command -v emar >/dev/null || { echo "em++ and emar are missing: source emsdk_env.sh"; exit 1; }

compile() {  # objects folder, library, its flags...: every source, changed ones only
  local objects="$1" library="$2"
  shift 2
  mkdir -p "$objects"
  find "$ROOT/source/$library/src" -maxdepth 1 -name '*.cpp' -print0 \
    | COMPILE_OBJECTS="$objects" COMPILE_FLAGS="$*" xargs -0 -n 1 -P "$JOBS" sh -c '
      object="$COMPILE_OBJECTS/$(basename "$1" .cpp).o"
      [ "$object" -nt "$1" ] || em++ $COMPILE_FLAGS -c "$1" -o "$object" \
        || { echo "failed: $1" >&2; exit 255; }
    ' sh
}

stage="$BUILD/stage"
rm -rf "$stage"
echo "==> SMLib and SM_API for $RUST_TARGET ($(em++ --version | head -1))"
objects="$BUILD/$RUST_TARGET"
compile "$objects/SMLib" SMLib "${COMMON[@]}" -I"$ROOT/source/SMLib/inc"
compile "$objects/SM_API" SM_API "${COMMON[@]}" -DSM_API_EXPORTS -I"$ROOT/source/SM_API/inc" -I"$ROOT/source/SMLib/inc"
mkdir -p "$stage/$RUST_TARGET/lib"
for library in SMLib SM_API; do
  rm -f "$stage/$RUST_TARGET/lib/lib$library.a"
  emar rcs "$stage/$RUST_TARGET/lib/lib$library.a" "$objects/$library/"*.o
done
ls -la "$stage/$RUST_TARGET/lib"

cp "$ROOT/LICENSE" "$stage/LICENSE"
echo "$VERSION" > "$stage/VERSION"
archive="$OUT/usd-brep-$VERSION-wasm.tar.gz"
tar -czf "$archive" -C "$stage" .
(cd "$OUT" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "$archive"
