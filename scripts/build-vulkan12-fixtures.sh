#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
output="${1:-tests/fixtures/bin}"
mkdir -p "$output"
headers="$PWD/target/vulkan-upstream/headers/Vulkan-Headers-cfebfc96b2b0bce93da7d12f2c14cc01793ae25c/include"
for source in vulkan12-index.comp vulkan12-views.vert vulkan12-views.frag vulkan12-input.frag vulkan12-ycbcr.comp vulkan12-atomic64.comp vulkan12-multiview.frag; do
    "${GLSLANG_VALIDATOR:-glslangValidator}" -V --target-env vulkan1.2 "tests/fixtures/$source" -o "$output/$source.spv"
    "${SPIRV_VAL:-spirv-val}" --target-env vulkan1.2 "$output/$source.spv"
done
"${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}" -O2 -Wall -Wextra -Werror -I "$headers" \
    -L "$PWD/target/vulkan-loader-sdk" -L "$PWD/target/alpine-build/lib" tests/fixtures/vulkan12-complete.c -lvulkan -lm -o "$output/vulkan12-complete"
