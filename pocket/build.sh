#!/usr/bin/env bash
# Build Oneiroi Pocket's engine as WebAssembly and inline it into oneiroi-pocket.html.
#   WASI_SDK=/path/to/wasi-sdk-25.0-x86_64-linux pocket/build.sh
# wasi-sdk: https://github.com/WebAssembly/wasi-sdk/releases (wasi-sdk-25). Needs the mpc-vst-plugins wrapper header:
#   MPC_VST=/path/to/mpc-vst-plugins (for wrapper/engine.h).
set -euo pipefail
cd "$(dirname "$0")"
: "${WASI_SDK:?set WASI_SDK}" "${MPC_VST:?set MPC_VST}"
"$WASI_SDK/bin/clang++" --target=wasm32-wasi --sysroot="$WASI_SDK/share/wasi-sysroot" -O2 -std=gnu++17 \
  -fno-exceptions -fvisibility=hidden -Wno-inconsistent-missing-override -mexec-model=reactor -msimd128 \
  -DNDEBUG -DONEIROI_PORTABLE_MINMAX -DONEIROI_INPUT \
  -I../engine/mpc -I../engine/src/befaco -I../engine/src/owl/LibSource -I../engine/src/owl/Source -I"$MPC_VST/wrapper" \
  -Wl,--strip-all -Wl,--export-dynamic -Wl,-z,stack-size=262144 -Wl,--initial-memory=33554432 \
  pocket.cc -o build/oneiroi.wasm
python3 inline.py build/oneiroi.wasm page.html oneiroi-pocket.html
ls -la build/oneiroi.wasm oneiroi-pocket.html
