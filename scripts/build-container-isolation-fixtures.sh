#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tests/fixtures/bin
"${AARCH64_CC:-aarch64-linux-gnu-gcc}" -static -O2 -pthread tests/fixtures/seccomp.c -o tests/fixtures/bin/seccomp
"${AARCH64_CC:-aarch64-linux-gnu-gcc}" -static -O2 tests/fixtures/network-namespaces.c -o tests/fixtures/bin/network-namespaces
"${HOST_CC:-cc}" -O2 -pthread tests/fixtures/seccomp.c -o tests/fixtures/bin/seccomp-native
