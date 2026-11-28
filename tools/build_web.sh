#!/bin/sh
# Builds the browser engine: source/dsp (the plug-in's engine, unchanged) behind web/uh20_web.cpp's C API, compiled
# to WebAssembly with Emscripten into docs/play/uh20.wasm, plus docs/play/build.json, which records the compiler and
# a hash of every source file so CI can tell when the committed engine no longer matches the source.
#   tools/build_web.sh            build (needs em++ on the PATH: brew install emscripten, or emsdk)
#   tools/build_web.sh --check    only check that docs/play/build.json matches the current sources
# Flags: -O3 like the native Release build, no -ffast-math (the native build has none), no SIMD,
# no threads (the engine uses none).
# SPDX-License-Identifier: AGPL-3.0-or-later
set -eu
cd "$(dirname "$0")/.."
SOURCES="source/dsp/UH20Engine.h source/dsp/JfetKit.h web/uh20_web.cpp tools/build_web.sh"
FLAGS="-std=c++20 -O3 -fno-exceptions -fno-rtti --no-entry -sSTANDALONE_WASM -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sSTACK_SIZE=1048576"
hash() { if command -v sha256sum >/dev/null; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
SRCHASH=$(hash $SOURCES | awk '{print $1}' | hash | awk '{print $1}')

if [ "${1:-}" = "--check" ]; then
  WANT=$(sed -n 's/.*"sources_sha256": "\([0-9a-f]*\)".*/\1/p' docs/play/build.json)
  if [ "$WANT" != "$SRCHASH" ]; then
    echo "FAIL docs/play/uh20.wasm was built from other sources than these: run tools/build_web.sh and commit docs/play"; exit 1
  fi
  WASM=$(hash docs/play/uh20.wasm | awk '{print $1}')
  if ! grep -q "\"wasm_sha256\": \"$WASM\"" docs/play/build.json; then
    echo "FAIL docs/play/uh20.wasm is not the file docs/play/build.json describes"; exit 1
  fi
  echo "PASS docs/play/uh20.wasm matches the sources"; exit 0
fi

OUT=${OUT:-docs/play/uh20.wasm}
# at most 4 compile threads (a busy machine stays responsive)
BINARYEN_CORES=${BINARYEN_CORES:-4} EMCC_CORES=${EMCC_CORES:-4} em++ $FLAGS web/uh20_web.cpp -o "$OUT"
VERSION=$(em++ --version | head -1 | sed 's/.*) \([0-9][0-9.]*\).*/\1/')
if [ "$OUT" = docs/play/uh20.wasm ]; then
  cat > docs/play/build.json <<JSON
{
  "about": "Written by tools/build_web.sh. CI checks sources_sha256 against the source and wasm_sha256 against the file.",
  "emscripten": "$VERSION",
  "flags": "$FLAGS",
  "sources": "$SOURCES",
  "sources_sha256": "$SRCHASH",
  "wasm_sha256": "$(hash "$OUT" | awk '{print $1}')"
}
JSON
fi
echo "built $OUT ($(wc -c < "$OUT" | tr -d ' ') bytes, emscripten $VERSION)"
