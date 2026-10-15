#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
mkdir -p tests/fixtures/bin
"$compiler" -static -O2 -Wall -Wextra -Werror tests/fixtures/devices.c -o tests/fixtures/bin/devices
"$compiler" -static -O2 -Wall -Wextra -Werror tests/fixtures/openat2.c -o tests/fixtures/bin/openat2
