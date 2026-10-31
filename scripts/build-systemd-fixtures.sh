#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
mkdir -p tests/fixtures/bin
for name in ofd-locks unix-notify mount-lifecycle systemd-boot; do
    "$compiler" -static -O2 -Wall -Wextra -Werror -pthread "tests/fixtures/$name.c" -o "tests/fixtures/bin/$name"
done
