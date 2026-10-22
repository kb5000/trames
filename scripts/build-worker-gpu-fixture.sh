#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}"
mkdir -p tests/fixtures/bin
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread \
  tests/fixtures/worker-gpu.c tests/fixtures/worker-gpu-native.S \
  tests/fixtures/epoch-services-native.S -o tests/fixtures/bin/worker-gpu
if [[ "${1:-}" == --standard-render ]]; then
  # Reuse the pinned, already prepared Vulkan fixture SDK; no downloads.
  headers="$PWD/target/vulkan-upstream/headers/Vulkan-Headers-cfebfc96b2b0bce93da7d12f2c14cc01793ae25c/include"
  "$compiler" -O2 -mno-outline-atomics -Wall -Wextra -Werror -pthread \
    -I "$headers" -L "$PWD/target/vulkan-loader-sdk" -L "$PWD/target/alpine-build/lib" \
    tests/fixtures/vulkan-render.c tests/fixtures/worker-gpu-peer.c \
    -Wl,--wrap=main -lvulkan -lm -o tests/fixtures/bin/worker-gpu-render
fi
