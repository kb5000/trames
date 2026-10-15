#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
mkdir -p tests/fixtures/bin
"$compiler" -static -O2 -Wall -Wextra -Werror tests/fixtures/unix-sockets.c -o tests/fixtures/bin/unix-sockets
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror tests/fixtures/unix-sockets.c -o tests/fixtures/bin/unix-sockets-host
"$compiler" -static -pthread -O2 -Wall -Wextra -Werror tests/fixtures/pdeathsig.c -o tests/fixtures/bin/pdeathsig
"${HOST_CC:-cc}" -pthread -O2 -Wall -Wextra -Werror tests/fixtures/pdeathsig.c -o tests/fixtures/bin/pdeathsig-host
"$compiler" -static -pthread -O2 -Wall -Wextra -Werror tests/fixtures/oom-score.c -o tests/fixtures/bin/oom-score
"${HOST_CC:-cc}" -pthread -O2 -Wall -Wextra -Werror tests/fixtures/oom-score.c -o tests/fixtures/bin/oom-score-host
