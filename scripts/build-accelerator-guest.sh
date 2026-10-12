#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
compiler="${MUSL_CC:-$(pwd)/target/busybox-build/musl/bin/musl-gcc}"
linker="${AARCH64_LD:-aarch64-linux-gnu-ld}"
output="$(pwd)/target/accelerator-guest"
mkdir -p "$output"
flags=(-O2 -fPIC -fno-builtin -fno-tree-vectorize -fno-tree-slp-vectorize -Wall -Wextra -Werror)
upstream=crates/trames-native/upstream/musl-1.2.5
# Keep upstream bytes untouched; private names prevent fallback interposition.
for name in memmove memcmp strlen; do
    "$compiler" "${flags[@]}" -Wno-parentheses -Dmemcpy=trames_orig_memcpy -Dmemmove=trames_orig_memmove -Dmemset=trames_orig_memset -Dmemcmp=trames_orig_memcmp -Dstrlen=trames_orig_strlen -c "$upstream/$name.c" -o "$output/$name.o"
done
for name in memcpy memset; do
    "$compiler" -fPIC -Dmemcpy=trames_orig_memcpy -Dmemset=trames_orig_memset -c "$upstream/aarch64/$name.S" -o "$output/$name.o"
done
"$compiler" "${flags[@]}" -c accelerator/guest/pure.c -o "$output/pure.o"
"$compiler" -c accelerator/guest/calls.S -o "$output/calls.o"
"$linker" -shared --version-script=accelerator/guest/exports.map --build-id=sha1 -soname libtrames-pure.so -o "$output/libtrames-pure.so" "$output/memcpy.o" "$output/memmove.o" "$output/memset.o" "$output/memcmp.o" "$output/strlen.o" "$output/pure.o" "$output/calls.o"
# Ship notices and exact guest-library corresponding source together.
cp "$upstream/COPYRIGHT" "$output/musl-COPYRIGHT.txt"
cp "$upstream/provenance.json" "$output/provenance.json"
cp LICENSE "$output/GPL-3.0.txt"
mkdir -p "$output/source/accelerator/guest" "$output/source/scripts" "$output/source/crates/trames-native/upstream"
cp accelerator/guest/pure.c accelerator/guest/calls.S accelerator/guest/exports.map "$output/source/accelerator/guest/"
cp scripts/build-accelerator-guest.sh "$output/source/scripts/"
cp -R "$upstream" "$output/source/crates/trames-native/upstream/"
cp LICENSE "$output/source/"
cat > "$output/SOURCE.txt" <<'NOTICE'
Trames pure-memory guest library, NativeCall protocol 1/module 1.
Trames wrapper: GPL-3.0-or-later. Original musl 1.2.5/Arm routines: MIT.
Exact corresponding guest-library source and script are in source/.
Build with a Linux AArch64 musl compiler and GNU linker:
  MUSL_CC=/path/to/musl-gcc AARCH64_LD=aarch64-linux-gnu-ld bash scripts/build-accelerator-guest.sh
Pinned compiler/rootfs fixture setup: https://github.com/kb5000/trames
Original upstream URL and fingerprints: provenance.json.
A guest manifest/library does not authorize host features. Enable pure v1 explicitly.
NOTICE
tar -C "$output" -czf "$output/libtrames-pure-v1.tar.gz" libtrames-pure.so musl-COPYRIGHT.txt GPL-3.0.txt provenance.json SOURCE.txt source
