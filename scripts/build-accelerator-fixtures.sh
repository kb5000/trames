#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/build-accelerator-guest.sh
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
output="$(pwd)/target/accelerator-guest"
flags=(-O2 -fPIC -fno-builtin -fno-tree-vectorize -fno-tree-slp-vectorize -Wall -Wextra -Werror)
mkdir -p tests/fixtures/bin
"$compiler" "${flags[@]}" -static tests/fixtures/accelerator.c "$output/memcpy.o" "$output/memmove.o" "$output/memset.o" "$output/memcmp.o" "$output/strlen.o" "$output/pure.o" "$output/calls.o" -o tests/fixtures/bin/accelerator
# A dynamic fixture resolves the actual shared-library symbols with dlsym.
"$compiler" "${flags[@]}" -DTRAMES_DYNAMIC -L"$(pwd)/target/alpine-build/lib" \
    -Wl,--dynamic-linker,/lib/ld-musl-aarch64.so.1 tests/fixtures/accelerator.c -ldl -o tests/fixtures/bin/accelerator-dynamic
