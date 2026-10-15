#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
mkdir -p tests/fixtures/bin
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread tests/fixtures/cgroups.c -o tests/fixtures/bin/cgroups
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread tests/fixtures/cgroup-control.c -o tests/fixtures/bin/cgroup-control
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread tests/fixtures/cgroup-pids.c -o tests/fixtures/bin/cgroup-pids
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread tests/fixtures/dumpable.c -o tests/fixtures/bin/dumpable
