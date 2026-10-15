#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
mkdir -p tests/fixtures/bin
"$compiler" -static -O2 -Wall -Wextra -Werror tests/fixtures/namespaces.c -o tests/fixtures/bin/namespaces

"$compiler" -static -O2 -Wall -Wextra -Werror tests/fixtures/direct-network.c -o tests/fixtures/bin/direct-network

"$compiler" -static -O2 -Wall -Wextra -Werror tests/fixtures/mount-namespaces.c -o tests/fixtures/bin/mount-namespaces
