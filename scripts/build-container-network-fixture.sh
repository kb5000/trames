#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tests/fixtures/bin
"${AARCH64_CC:-aarch64-linux-gnu-gcc}" -static -O2 -Wall -Wextra -Werror tests/fixtures/container-network.c -o tests/fixtures/bin/container-network
# Existing namespace/old-FD behavior is affected by sharing the new fabric.
"${AARCH64_CC:-aarch64-linux-gnu-gcc}" -static -O2 tests/fixtures/network-namespaces.c -o tests/fixtures/bin/network-namespaces

"${AARCH64_CC:-aarch64-linux-gnu-gcc}" -static -O2 -Wall -Wextra -Werror tests/fixtures/container-forwarding.c -o tests/fixtures/bin/container-forwarding
