#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
python3 scripts/prepare-vulkan.py
if [[ ! -x target/busybox-build/musl/bin/musl-gcc ]]; then bash scripts/build-busybox.sh; fi
if [[ ! -f target/alpine-build/lib/libc.so ]]; then bash scripts/prepare-alpine.sh; fi
compiler="${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}"
headers="$PWD/target/vulkan-upstream/headers/Vulkan-Headers-cfebfc96b2b0bce93da7d12f2c14cc01793ae25c/include"
output="$PWD/target/vulkan-guest"
mkdir -p "$output"
python3 - <<'PY'
from pathlib import Path
import re
source=''.join(Path('accelerator/vulkan/'+p).read_text() for p in ['icd.c','graphics.inc','queries.inc','zink.inc','templates.inc','wsi.inc','wayland.inc'])
names=re.findall(r'VKAPI_CALL (vk\w+)\(',source)
expected='// SPDX-License-Identifier: GPL-3.0-or-later\n'+''.join(f'    if(!strcmp(name,"{name}"))return (PFN_vkVoidFunction){name};\n' for name in names if not name.startswith('vk_icd'))
assert Path('accelerator/vulkan/entrypoints.inc').read_text()==expected, 'refresh entrypoint catalog after changing exports'
PY
# The ICD must use the process's original dynamic libc. Accidentally embedding
# static musl adds private auxv/loader state and breaks feature negotiation.
"$compiler" -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror -Wno-misleading-indentation \
  -I "$headers" -I accelerator/vulkan -shared -nostdlib \
  accelerator/vulkan/icd.c accelerator/vulkan/calls.S "$PWD/target/alpine-build/lib/libc.so" \
  -Wl,--build-id=sha1 -Wl,-z,noexecstack -o "$output/libtrames_vulkan_icd.so"
"${AARCH64_READELF:-aarch64-linux-gnu-readelf}" -d "$output/libtrames_vulkan_icd.so" | awk '/NEEDED.*libc.musl-aarch64.so.1/ { found=1 } END { exit !found }'
python3 - <<'PY'
from pathlib import Path
import hashlib,json
p=Path('target/vulkan-guest')
(p/'trames.json').write_text(json.dumps({'file_format_version':'1.0.0','ICD':{'api_version':'1.1.0','library_path':'/usr/lib/libtrames_vulkan_icd.so'}},indent=2)+'\n')
v={'profile':65542,'auxv_key':'0x54520008','wire_revision':5,'experimental':True,'instance_api':'1.1.0','device_api':'1.0.0','scope':'selected compute, graphics, queries, descriptor templates, Wayland and fixed-display WSI API; not Vulkan conformance or complete Zink','icd_sha256':hashlib.sha256((p/'libtrames_vulkan_icd.so').read_bytes()).hexdigest(),'loader':'unmodified distro libvulkan.so.1','headers':'1.3.281','native_moltenvk':'1.4.2','default_enabled':False}
(p/'manifest.json').write_text(json.dumps(v,indent=2)+'\n')
PY
cp LICENSE "$output/GPL-3.0.txt"
