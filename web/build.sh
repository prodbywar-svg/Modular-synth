#!/bin/sh
# Builds the browser version into web/site:
#   engine.wasm - the same engine as the desktop app, compiled to WebAssembly
#   hr16/       - the HR-16 sounds
# Needs clang, lld and wasi-libc + libc++ for wasm32 (Ubuntu: apt install
# clang lld wasi-libc libc++-18-dev-wasm32 libc++abi-18-dev-wasm32 libclang-rt-18-dev-wasm32).
set -e
cd "$(dirname "$0")/.."

CXX=${CXX:-clang++}
OUT=web/site

$CXX --target=wasm32-wasi -std=c++17 -O3 -DNDEBUG -DMS_NO_JUCE=1 \
    -fno-exceptions -fvisibility=hidden -mexec-model=reactor \
    -ISource \
    Source/engine/Params.cpp \
    Source/engine/ProjectState.cpp \
    Source/engine/Voice.cpp \
    Source/engine/FxChain.cpp \
    Source/engine/Effects.cpp \
    Source/engine/Hr16Bank.cpp \
    Source/engine/EngineCore.cpp \
    web/wasm/WebEngine.cpp \
    -Wl,--export-dynamic -Wl,--strip-all -Wl,-z,stack-size=1048576 \
    -o $OUT/engine.wasm

mkdir -p $OUT/hr16
cp assets/hr16/*.wav $OUT/hr16/
echo "Built $OUT/engine.wasm ($(wc -c < $OUT/engine.wasm) bytes)"
