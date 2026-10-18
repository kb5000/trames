#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tests/fixtures/bin
"${AARCH64_CC:-aarch64-linux-gnu-gcc}" -static -O2 -Wall -Wextra -Werror tests/fixtures/nftables.c -o tests/fixtures/bin/nftables
