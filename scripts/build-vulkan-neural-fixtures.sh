#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Focused fixture build; reuse the already validated ICD, loader and native SDK.
set -euo pipefail
cd "$(dirname "$0")/.."
output="${1:-tests/fixtures/bin}"
mkdir -p "$output"
headers="$PWD/target/vulkan-upstream/headers/Vulkan-Headers-cfebfc96b2b0bce93da7d12f2c14cc01793ae25c/include"
"${GLSLANG_VALIDATOR:-glslangValidator}" -V --target-env vulkan1.1 tests/fixtures/vulkan-neural.comp -o "$output/vulkan-neural.spv"
"${SPIRV_AS:-spirv-as}" --target-env vulkan1.1 tests/fixtures/vulkan-neural-dot.spvasm -o "$output/vulkan-neural-dot.spv"
for name in vulkan-neural vulkan-neural-dot; do
    "${SPIRV_VAL:-spirv-val}" --target-env vulkan1.1 "$output/$name.spv"
done
"${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}" -O2 -Wall -Wextra -Werror \
    -I "$headers" -L "$PWD/target/vulkan-loader-sdk" -L "$PWD/target/alpine-build/lib" \
    tests/fixtures/vulkan-neural.c -lvulkan -o "$output/vulkan-neural"

# The optional native filter changes discovery only, to exercise shadow coherence.
if [[ $(uname -s) == Linux ]]; then
    "${HOST_CC:-cc}" -O2 -fPIC -shared -Wall -Wextra -Werror -Wno-misleading-indentation \
        -I "$headers" tests/fixtures/vulkan-no-host-import.c -pthread -ldl \
        -o "$output/vulkan-no-host-import.so"
fi
