#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
mkdir -p tests/fixtures/bin
for name in fd-exec fd-exec-child; do
    "$compiler" -static -O2 -Wall -Wextra -Werror "tests/fixtures/$name.c" -o "tests/fixtures/bin/$name"
    "${HOST_CC:-cc}" -static -O2 -Wall -Wextra -Werror "tests/fixtures/$name.c" -o "tests/fixtures/bin/$name-host"
done
