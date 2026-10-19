#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}"
if [[ -z "${MUSL_CC:-}" && ! -x "$compiler" ]]; then
  build="$PWD/target/busybox-build"
  mkdir -p "$build/downloads"
  source="$build/downloads/musl-1.2.5.tar.gz"
  if [[ ! -f "$source" ]]; then
    curl --fail --location --max-time 120 https://musl.libc.org/releases/musl-1.2.5.tar.gz -o "$source"
  fi
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
fi
mkdir -p tests/fixtures/bin
"$compiler" -static -O2 -Wall -Wextra -Werror -pthread \
  tests/fixtures/epoch-services.c tests/fixtures/epoch-services-native.S \
  -o tests/fixtures/bin/epoch-services
