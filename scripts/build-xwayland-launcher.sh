#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
output="${TRAMES_XWAYLAND_OUTPUT:-target/xwayland}"
mkdir -p "$output"
"${MUSL_CC:-target/busybox-build/musl/bin/musl-gcc}" -O2 -mno-outline-atomics \
  -Wall -Wextra -Werror accelerator/xwayland/launcher.c -o "$output/trames-xwayland"
python3 - "$output" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1])
(p/'launcher-manifest.json').write_text(json.dumps(dict(
    architecture='aarch64-linux-musl',license='GPL-3.0-or-later',
    source='accelerator/xwayland/launcher.c',
    sha256=hashlib.sha256((p/'trames-xwayland').read_bytes()).hexdigest()),indent=2)+'\n')
PY
