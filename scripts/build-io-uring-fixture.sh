#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}"
headers="${LINUX_UAPI_HEADERS:-/workspace/toolchains/cross/usr/aarch64-linux-gnu/include}"
mkdir -p tests/fixtures/bin
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread -idirafter "$headers" \
  tests/fixtures/io-uring.c -o tests/fixtures/bin/io-uring
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread -idirafter "$headers" \
  tests/fixtures/io-uring-extended.c -o tests/fixtures/bin/io-uring-extended
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread -idirafter "$headers" \
  tests/fixtures/io-uring-multishot.c -o tests/fixtures/bin/io-uring-multishot
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread -idirafter "$headers" \
  tests/fixtures/io-uring-worker.c -o tests/fixtures/bin/io-uring-worker
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread -idirafter "$headers" \
  tests/fixtures/io-uring-worker-registered.c -o tests/fixtures/bin/io-uring-worker-registered
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread -idirafter "$headers" \
  tests/fixtures/io-uring-worker-async.c -o tests/fixtures/bin/io-uring-worker-async
