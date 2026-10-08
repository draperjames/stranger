#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
python3 scripts/generate_metadata.py
image="${STRANGER_BUILD_IMAGE:-stranger-builder}"
docker build -t "$image" -f scripts/Dockerfile .
docker run --rm -v "$root:/build" -w /build -u "$(id -u):$(id -g)" "$image" sh -ec '
    mkdir -p build/arm64 dist/stranger
    flags="-O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -march=armv8-a -mtune=cortex-a72"
    aarch64-linux-gnu-gcc $flags -fPIC -fvisibility=hidden -shared src/stranger.c -o build/arm64/stranger.so -lm
    aarch64-linux-gnu-gcc $flags tests/test_stranger.c -o build/arm64/test_stranger -ldl -lm
    file build/arm64/stranger.so
    aarch64-linux-gnu-nm -D build/arm64/stranger.so | grep " T move_audio_fx_init_v2$"
    cp build/arm64/stranger.so src/module.json src/help.json LICENSE THIRD_PARTY.md AI_DISCLOSURE.md README.md dist/stranger/
    mkdir -p dist/stranger/docs
    cp docs/PROVENANCE.md docs/VALIDATION.md dist/stranger/docs/
    mkdir -p dist/stranger/LICENSES
    cp LICENSES/SCHWUNG-MIT.txt dist/stranger/LICENSES/
    chmod 755 dist/stranger/stranger.so
    tar -C dist -czf dist/stranger-module.tar.gz stranger
'
python3 tests/test_package.py
