#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
workspace="$(pwd)/target/busybox-build"
downloads="${TRAMES_SOURCE_CACHE:-$workspace/downloads}"
compiler="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
archiver="${AARCH64_AR:-aarch64-linux-gnu-ar}"
ranlib="${AARCH64_RANLIB:-aarch64-linux-gnu-ranlib}"
jobs="${TRAMES_BUILD_JOBS:-4}"
mkdir -p "$workspace" "$downloads"
fetch() {
    local name="$1" url="$2" digest="$3"
    if [[ ! -f "$downloads/$name" ]]; then
        curl --fail --location --silent --show-error "$url" --output "$downloads/$name"
    fi
    (cd "$downloads" && printf '%s  %s\n' "$digest" "$name" | sha256sum --check)
}
fetch musl-1.2.5.tar.gz https://musl.libc.org/releases/musl-1.2.5.tar.gz a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
fetch busybox-1.37.0.tar.bz2 https://busybox.net/downloads/busybox-1.37.0.tar.bz2 3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4
if [[ ! -x "$workspace/musl/bin/musl-gcc" ]]; then
    tar -xf "$downloads/musl-1.2.5.tar.gz" -C "$workspace"
    (
        cd "$workspace/musl-1.2.5"
        CC="$compiler" AR="$archiver" RANLIB="$ranlib" CFLAGS='-O2 -fno-tree-vectorize -fno-tree-slp-vectorize' ./configure --target=aarch64-linux-musl --prefix="$workspace/musl" --disable-shared --enable-gcc-wrapper
        make -j "$jobs"
        make install
    )
fi
if [[ ! -d "$workspace/busybox-1.37.0" ]]; then
    tar -xf "$downloads/busybox-1.37.0.tar.bz2" -C "$workspace"
fi
(
    cd "$workspace/busybox-1.37.0"
    make allnoconfig
    python3 - <<'PY'
from pathlib import Path
enabled = {'STATIC', 'BUSYBOX', 'ECHO', 'FEATURE_FANCY_ECHO', 'TRUE', 'FALSE',
           'CAT', 'WC', 'HEAD', 'TAIL', 'PWD', 'LS', 'FEATURE_LS_SORTFILES',
           'BASENAME', 'DIRNAME', 'PRINTF'}
p = Path('.config')
lines = p.read_text().splitlines()
for index, line in enumerate(lines):
    for name in enabled:
        key = 'CONFIG_' + name
        if line.startswith(key + '=') or line == '# ' + key + ' is not set':
            lines[index] = key + '=y'
p.write_text('\n'.join(lines) + '\n')
import subprocess
subprocess.run(['make', 'oldconfig'], input='\n' * 2000, text=True, check=True)
PY
    make -j "$jobs" CC="$workspace/musl/bin/musl-gcc" AR="$archiver" CFLAGS='-O2 -fno-tree-vectorize -fno-tree-slp-vectorize' SKIP_STRIP=y
)
cp "$workspace/busybox-1.37.0/busybox_unstripped" "$workspace/busybox"
printf 'Static BusyBox: %s\n' "$workspace/busybox"
