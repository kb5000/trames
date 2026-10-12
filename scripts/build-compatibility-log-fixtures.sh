#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
mkdir -p tests/fixtures/bin
"$compiler" -static -O2 -fno-tree-vectorize -fno-tree-slp-vectorize -Wall -Wextra -Werror tests/fixtures/compatibility-log.c -o tests/fixtures/bin/compatibility-log
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror tests/fixtures/compatibility-log.c -o tests/fixtures/bin/compatibility-log-host
