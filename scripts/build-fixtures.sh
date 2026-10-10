#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
assembler="${AARCH64_AS:-aarch64-linux-gnu-as}"
linker="${AARCH64_LD:-aarch64-linux-gnu-ld}"
mkdir -p tests/fixtures/bin
for source in tests/fixtures/*.S; do
    name="$(basename "$source" .S)"
    "$assembler" -o "tests/fixtures/bin/$name.o" "$source"
    "$linker" -static --build-id=none -z noexecstack -o "tests/fixtures/bin/$name" "tests/fixtures/bin/$name.o"
done
