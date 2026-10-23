#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
git submodule update --init datasrc/maps
cmake -S . -B build -G Ninja -DCLIENT=OFF -DDOWNLOAD_GTEST=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target teeworlds_bridge teeworlds_replayer -j"${BUILD_JOBS:-4}"
python3 scripts/export-map.py
cargo build --locked --manifest-path myelin/Cargo.toml
mkdir -p release
cp build/teeworlds_bridge release/
cp datasrc/maps/dm1.map release/
cp myelin/game.cfg release/
cp "${CARGO_TARGET_DIR:-myelin/target}/debug/myelin-teeworlds" release/
# The native bridge bytes are part of the Myelin application profile. Do not strip
# or rebuild it after compiling the Rust verifier; rebuild both together.
