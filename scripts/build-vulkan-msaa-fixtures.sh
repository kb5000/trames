#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Focused graphics oracles; reuse the paired ICD, loader, Mesa and native SDK.
set -euo pipefail
cd "$(dirname "$0")/.."
output="${1:-tests/fixtures/bin}"
mkdir -p "$output"
headers="$PWD/target/vulkan-upstream/headers/Vulkan-Headers-cfebfc96b2b0bce93da7d12f2c14cc01793ae25c/include"
compiler="${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}"
for stage in vert frag; do
    "${GLSLANG_VALIDATOR:-glslangValidator}" -V --target-env vulkan1.2 "tests/fixtures/vulkan-msaa.$stage" -o "$output/vulkan-msaa.$stage.spv"
    "${SPIRV_VAL:-spirv-val}" --target-env vulkan1.2 "$output/vulkan-msaa.$stage.spv"
done
"$compiler" -O2 -Wall -Wextra -Werror -I "$headers" \
    -L "$PWD/target/vulkan-loader-sdk" -L "$PWD/target/alpine-build/lib" \
    tests/fixtures/vulkan-msaa.c -lvulkan -o "$output/vulkan-msaa"
# Optional original GLES oracle, linked with the same Mesa/GLES stack under test.
if [[ -n "${MESA_INCLUDE:-}" ]]; then
    "$compiler" -O2 -Wall -Wextra -Werror -I "$MESA_INCLUDE" tests/fixtures/zink-msaa.c \
        "${GLES_LIBRARY:?set the paired GLES library}" "${EGL_LIBRARY:?set the paired EGL library}" \
        "${GALLIUM_LIBRARY:?set the paired Gallium library}" -o "$output/zink-msaa"
fi
