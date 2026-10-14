#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
mkdir -p tests/fixtures/bin
"$compiler" -static -O2 -Wall -Wextra -Werror tests/fixtures/pty.c -o tests/fixtures/bin/pty
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror tests/fixtures/pty.c -o tests/fixtures/bin/pty-host
