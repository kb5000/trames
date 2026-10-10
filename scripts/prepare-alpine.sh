#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
workspace="$(pwd)/target/alpine-build"
downloads="${TRAMES_ALPINE_CACHE:-$(pwd)/target/alpine-download}"
mkdir -p "$workspace" "$downloads" "$workspace/repo/aarch64"
fetch() {
    local name="$1" url="$2" digest="$3"
    if [[ ! -f "$downloads/$name" ]]; then
        curl --fail --location --silent --show-error --max-time 120 "$url" --output "$downloads/$name"
    fi
    (cd "$downloads" && printf '%s  %s\n' "$digest" "$name" | sha256sum --check)
}
fetch alpine-minirootfs-3.22.6-aarch64.tar.gz https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/alpine-minirootfs-3.22.6-aarch64.tar.gz 821565fa8f3953eefd12497b166b4b50add2f7c57fb312e75862f5867e06fefe
fetch tree-2.2.1-r0.apk https://dl-cdn.alpinelinux.org/alpine/v3.22/main/aarch64/tree-2.2.1-r0.apk 75a88e6bff724b5db296210310a54dca3695d666f8016e87f28df7af9e9b226b
# This live upstream index is authenticated by apk against pinned-rootfs Alpine
# public keys in the integration test, rather than trusted as arbitrary tar data.
if [[ ! -f "$downloads/APKINDEX.tar.gz" ]]; then
    curl --fail --location --silent --show-error --max-time 120 https://dl-cdn.alpinelinux.org/alpine/v3.22/main/aarch64/APKINDEX.tar.gz --output "$downloads/APKINDEX.tar.gz"
fi
cp "$downloads/APKINDEX.tar.gz" "$downloads/tree-2.2.1-r0.apk" "$workspace/repo/aarch64/"
if [[ ! -f "$workspace/rootfs/etc/alpine-release" ]]; then
    mkdir -p "$workspace/rootfs"
    tar -xzf "$downloads/alpine-minirootfs-3.22.6-aarch64.tar.gz" -C "$workspace/rootfs"
fi
mkdir -p "$workspace/lib" tests/fixtures/bin
ln -sf "$workspace/rootfs/lib/ld-musl-aarch64.so.1" "$workspace/lib/libc.so"
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
"$compiler" -O2 -fno-tree-vectorize -fno-tree-slp-vectorize -fPIC -shared -L"$workspace/lib" -Wl,-soname,libm3probe.so tests/fixtures/m3-library.c -o tests/fixtures/bin/libm3probe.so
for type in pie exec; do
    flags=(-fPIE -pie)
    if [[ "$type" == exec ]]; then flags=(-fno-pie -no-pie); fi
    "$compiler" -O2 -fno-tree-vectorize -fno-tree-slp-vectorize "${flags[@]}" -L"$workspace/lib" -Wl,--dynamic-linker,/lib/ld-musl-aarch64.so.1 tests/fixtures/m3.c -ldl -o "tests/fixtures/bin/m3-$type"
done
printf 'Verified Alpine 3.22.6, signed repository fixtures and dynamic ELF fixtures prepared.\n'
