#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p target/allocator-sanitizers
python3 crates/trames-native/allocator/generate.py target/allocator-sanitizers/allocator.c
"${CC:-cc}" -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all \
    target/allocator-sanitizers/allocator.c tests/fixtures/native-allocator-host.c \
    -o target/allocator-sanitizers/check
# This harness has no heap allocations. Keep ASan/UBSan enabled; LSan cannot
# inspect threads in ptrace-backed workspaces.
ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" target/allocator-sanitizers/check
