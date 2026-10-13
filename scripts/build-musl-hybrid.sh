#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
output="$(pwd)/target/musl-hybrid"
downloads="${TRAMES_SOURCE_CACHE:-$(pwd)/target/busybox-build/downloads}"
compiler="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
mkdir -p "$output" "$downloads"
archive="$downloads/musl-1.2.5.tar.gz"
if [[ ! -f "$archive" ]]; then
    curl --fail --location --silent --show-error --max-time 120 https://musl.libc.org/releases/musl-1.2.5.tar.gz --output "$archive"
fi
printf '%s  %s\n' a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4 "$archive" | sha256sum --check
# Fresh source avoids mixing cached patch/configuration versions.
build=$(mktemp -d "$output/build.XXXXXXXX")
trap 'rm -rf "$build"' EXIT
tar -xf "$archive" -C "$build"
source_tree="$build/musl-1.2.5"
python3 scripts/prepare-musl-hybrid.py "$source_tree"
(
    cd "$source_tree"
    CC="$compiler" AR="${AARCH64_AR:-aarch64-linux-gnu-ar}" RANLIB="${AARCH64_RANLIB:-aarch64-linux-gnu-ranlib}" \
        CFLAGS='-O2 -fno-tree-vectorize -fno-tree-slp-vectorize' LDFLAGS='-Wl,--build-id=sha1 -Wl,-z,noexecstack' \
        ./configure --target=aarch64-linux-musl --prefix=/usr --syslibdir=/lib --enable-shared
    make -j "${TRAMES_BUILD_JOBS:-4}" lib/libc.so
)
cp "$source_tree/lib/libc.so" "$output/libc.so"
cp "$source_tree/COPYRIGHT" "$output/musl-COPYRIGHT.txt"
cp "$source_tree/trames-patch.json" "$output/trames-patch.json"
cp LICENSE "$output/GPL-3.0.txt"
python3 - "$output" "$compiler" <<'PY'
import hashlib,json,pathlib,subprocess,sys
p=pathlib.Path(sys.argv[1]);v={'upstream':'musl-1.2.5','upstream_archive_sha256':'a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4','libc_sha256':hashlib.sha256((p/'libc.so').read_bytes()).hexdigest(),'guest_loader':'/lib/ld-musl-aarch64.so.1','guest_libc_alias':'/lib/libc.musl-aarch64.so.1','native_profile':65537,'native_features':524287,'native_functions':['memcpy','memmove','memset','memcmp','strlen','memchr','memrchr','strnlen','strcmp','strncmp','strchr','strrchr','strchrnul','strcpy','stpcpy','strncpy','stpncpy','strcat','strncat'],'callback_profile':65538,'callback_functions':['qsort','qsort_r','bsearch'],'callback_threshold_elements':32,'native_bsearch_default':False,'integer_profile':65540,'integer_functions':['strtol','strtoul','strtoll','strtoull','strtoimax','strtoumax'],'integer_bits':64,'numeric_profile':65539,'numeric_functions':['strtod','strtof','atof','strtod_l','strtof_l'],'numeric_max_input_bytes':65536,'numeric_intermediate_bits':53,'numeric_non_nearest_fallback':True,'callback_max_sort_bytes':65536,'native_max_bulk_bytes':1048576,'native_max_string_bytes':1048576,'short_operation_threshold':128,'strlen_prefix_bytes':64,'strnlen_prefix_bytes':64,'compiler':subprocess.check_output([sys.argv[2],'--version']).decode().splitlines()[0]}
(p/'manifest.json').write_text(json.dumps(v,indent=2)+'\n')
PY
# Exact original archive plus complete patches/build scripts provide the source.
package="$build/package"
mkdir -p "$package/source/accelerator/guest" "$package/source/accelerator/musl" "$package/source/scripts" "$package/source/target/busybox-build/downloads"
cp "$output/libc.so" "$output/manifest.json" "$output/musl-COPYRIGHT.txt" "$output/GPL-3.0.txt" "$output/trames-patch.json" "$package/"
cp "$archive" "$package/source/target/busybox-build/downloads/"
cp accelerator/guest/pure.c accelerator/guest/calls.S "$package/source/accelerator/guest/"
cp accelerator/musl/init.c accelerator/musl/symbols.h accelerator/musl/callbacks.c accelerator/musl/callbacks.h accelerator/musl/callback-calls.S accelerator/musl/sort-symbols.h accelerator/musl/numeric.c accelerator/musl/numeric-symbols.h accelerator/musl/numeric-calls.S accelerator/musl/integer.c accelerator/musl/integer-symbols.h accelerator/musl/integer-calls.S accelerator/musl/strings.c accelerator/musl/strings-symbols.h "$package/source/accelerator/musl/"
cp scripts/prepare-musl-hybrid.py scripts/build-musl-hybrid.sh "$package/source/scripts/"
cp LICENSE "$package/source/"
tar -C "$package" -czf "$output/musl-hybrid-1.2.5-source.tar.gz" .
printf 'Complete hybrid musl built: %s\n' "$output/libc.so"
