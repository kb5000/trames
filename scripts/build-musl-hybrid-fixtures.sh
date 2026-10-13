#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
flags=(-O2 -fno-builtin -fno-tree-vectorize -fno-tree-slp-vectorize -L"$(pwd)/target/alpine-build/lib" -Wl,--dynamic-linker,/lib/ld-musl-aarch64.so.1)
"$compiler" "${flags[@]}" -DTRAMES_DYNAMIC -DTRAMES_FULL_LIBRARY tests/fixtures/accelerator.c -pthread -ldl -o tests/fixtures/bin/accelerator-full-musl
"$compiler" "${flags[@]}" tests/fixtures/musl-startup.c -pthread -ldl -lm -o tests/fixtures/bin/musl-startup
"$compiler" "${flags[@]}" tests/fixtures/runtime-compat.c -pthread -o tests/fixtures/bin/runtime-compat-dynamic
"$compiler" "${flags[@]}" tests/fixtures/musl-callbacks.c -pthread -o tests/fixtures/bin/musl-callbacks
"$compiler" "${flags[@]}" tests/fixtures/musl-numeric.c -pthread -lm -o tests/fixtures/bin/musl-numeric
"$compiler" "${flags[@]}" tests/fixtures/musl-stdlib.c -pthread -ldl -o tests/fixtures/bin/musl-stdlib
