#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build only the new clock oracle; keep prior socket/memory artifacts unchanged.
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}"
if [[ ! -x "$compiler" ]]; then
  build="$PWD/target/firefox-clock-build"
  mkdir -p "$build"
  source="$PWD/target/firefox-fixture/musl-1.2.5.tar.gz"
  printf '%s  %s\n' a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4 "$source" | sha256sum --check
  tar -xf "$source" -C "$build"
  (
    cd "$build/musl-1.2.5"
    CC="${AARCH64_CC:-aarch64-linux-gnu-gcc}" AR="${AARCH64_AR:-aarch64-linux-gnu-ar}" \
      RANLIB="${AARCH64_RANLIB:-aarch64-linux-gnu-ranlib}" \
      CFLAGS='-O2 -fno-tree-vectorize -fno-tree-slp-vectorize' \
      ./configure --target=aarch64-linux-musl --prefix="$build/musl" --disable-shared --enable-gcc-wrapper
    make -j4
    make install
  )
  compiler="$build/musl/bin/musl-gcc"
fi
mkdir -p target/firefox-clock-fixture
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread \
  tests/fixtures/firefox-clock-probe.c -o target/firefox-clock-fixture/clock-probe
cp tests/fixtures/firefox-clock-probe.c LICENSE target/firefox-clock-fixture/
