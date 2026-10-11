#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Independent AArch64 oracle: older QEMU corrupts scalar FP16 fixed-point results.
set -euo pipefail
cd "$(dirname "$0")/.."
version=10.0.13
digest=4a59b74dac7ead8b162a7c4595dcbbeebe989b90c89cfe6bb4dec5447342f6fa
prefix="$(pwd)/target/qemu-oracle"
if [[ -x "$prefix/bin/qemu-aarch64" ]]; then
    oracle_banner=$("$prefix/bin/qemu-aarch64" --version)
    oracle_line=${oracle_banner%%$'\n'*}
    case "$oracle_line" in
        "qemu-aarch64 version $version"|"qemu-aarch64 version $version ("*)
            printf '%s\n' "$oracle_line" ;;
        *) printf 'Unexpected cached oracle: %s\n' "$oracle_line" >&2; exit 1 ;;
    esac
    exit 0
fi
work="$(pwd)/target/qemu-oracle-build"
mkdir -p "$work"
archive="$work/qemu-$version.tar.xz"
if [[ ! -f "$archive" ]]; then
    curl --fail --location --retry 3 "https://download.qemu.org/qemu-$version.tar.xz" --output "$archive"
fi
printf '%s  %s\n' "$digest" "$archive" | sha256sum --check --status
tar -xf "$archive" -C "$work"
mkdir -p "$work/build"
cd "$work/build"
"$work/qemu-$version/configure" --target-list=aarch64-linux-user \
    --prefix="$prefix" --without-default-features --enable-linux-user --disable-system \
    --disable-docs --disable-tools --disable-guest-agent --disable-werror
ninja -j "${TRAMES_ORACLE_JOBS:-2}" qemu-aarch64
install -D -m 755 qemu-aarch64 "$prefix/bin/qemu-aarch64"
"$prefix/bin/qemu-aarch64" --version
