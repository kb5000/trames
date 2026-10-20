#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}"
if [[ -z "${MUSL_CC:-}" && ! -x "$compiler" ]]; then
  # Reuse the existing pinned, original musl toolchain preparation.
  bash scripts/build-epoch-services-fixture.sh
fi
out="$PWD/target/firefox-fixture"
mkdir -p "$out"
"$compiler" -static -O2 -Wall -Wextra -Werror tests/fixtures/firefox-socket-probe.c -o "$out/socket-probe"
cp tests/fixtures/firefox-socket-probe.c "$out/"
cp LICENSE "$out/"
if [[ -z "${MUSL_CC:-}" ]]; then
  tar -xOf target/busybox-build/downloads/musl-1.2.5.tar.gz musl-1.2.5/COPYRIGHT >"$out/musl-COPYRIGHT"
  cp target/busybox-build/downloads/musl-1.2.5.tar.gz "$out/"
fi
