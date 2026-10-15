#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/build-vulkan-guest.sh
mkdir -p tests/fixtures/bin target/vulkan-loader-sdk
python3 - <<'PY'
import importlib.util,tarfile,tempfile,shutil
from pathlib import Path
spec=importlib.util.spec_from_file_location('m6',Path('tests/integration/m6.py')); m6=importlib.util.module_from_spec(spec);spec.loader.exec_module(m6)
with tempfile.TemporaryDirectory(prefix='vulkan-sdk-') as directory:
    repository=Path(directory)/'repository'
    m6.repository(repository,('vulkan-loader',))
    acceptance=Path('target/vulkan-guest/acceptance');acceptance.mkdir(parents=True,exist_ok=True)
    for item in repository.rglob('*.apk'):shutil.copyfile(item,acceptance/item.name)
    shutil.copyfile(m6.CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz',acceptance/'alpine-minirootfs.tar.gz')
    package=next(repository.rglob('vulkan-loader-*.apk'))
    with tarfile.open(package,ignore_zeros=True) as archive:
        for entry in archive:
            if entry.isfile() and '/libvulkan.so.' in entry.name:
                Path('target/vulkan-loader-sdk/libvulkan.so').write_bytes(archive.extractfile(entry).read())
PY
headers="$PWD/target/vulkan-upstream/headers/Vulkan-Headers-cfebfc96b2b0bce93da7d12f2c14cc01793ae25c/include"
"${GLSLANG_VALIDATOR:-glslangValidator}" -V --target-env vulkan1.0 tests/fixtures/vulkan-compute.comp -o tests/fixtures/bin/vulkan-compute.spv
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror -I "$headers" tests/fixtures/vulkan-compute.c \
  "${VULKAN_HOST_LINK:--lvulkan}" -ldl -o tests/fixtures/bin/vulkan-compute-host
"${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}" -O2 -mno-outline-atomics -Wall -Wextra -Werror \
  -I "$headers" -L "$PWD/target/vulkan-loader-sdk" -L "$PWD/target/alpine-build/lib" \
  tests/fixtures/vulkan-compute.c -lvulkan -ldl -o tests/fixtures/bin/vulkan-compute
cp tests/fixtures/bin/vulkan-compute tests/fixtures/bin/vulkan-compute.spv target/vulkan-guest/acceptance/
for stage in vert frag; do
  "${GLSLANG_VALIDATOR:-glslangValidator}" -V --target-env vulkan1.0 "tests/fixtures/vulkan-render.$stage" -o "tests/fixtures/bin/vulkan-render.$stage.spv"
done
"${HOST_CC:-cc}" -O2 -Wall -Wextra -Werror -I "$headers" tests/fixtures/vulkan-render.c \
  "${VULKAN_HOST_LINK:--lvulkan}" -lm -o tests/fixtures/bin/vulkan-render-host
"${MUSL_CC:-$PWD/target/busybox-build/musl/bin/musl-gcc}" -O2 -mno-outline-atomics -Wall -Wextra -Werror \
  -I "$headers" -L "$PWD/target/vulkan-loader-sdk" -L "$PWD/target/alpine-build/lib" \
  tests/fixtures/vulkan-render.c -lvulkan -lm -o tests/fixtures/bin/vulkan-render
cp tests/fixtures/bin/vulkan-render tests/fixtures/bin/vulkan-render.*.spv target/vulkan-guest/acceptance/
