#!/usr/bin/env bash
# Original libwayland client ABI; no generated Rust client shares server code.
set -euo pipefail
cd "$(dirname "$0")/.."
output="$PWD/target/wayland"
mkdir -p "$output/generated"
cargo metadata --locked --format-version 1 > "$output/cargo-metadata.json"
xml=$(python3 - "$output/cargo-metadata.json" <<'PY'
import json,pathlib,sys
p=next(p for p in json.load(open(sys.argv[1]))['packages'] if p['name']=='wayland-protocols')
print(pathlib.Path(p['manifest_path']).parent/'protocols/stable/xdg-shell/xdg-shell.xml')
PY
)
scanner=${TRAMES_WAYLAND_SCANNER:-wayland-scanner}
"$scanner" client-header "$xml" "$output/generated/xdg-shell-client-protocol.h"
"$scanner" private-code "$xml" "$output/generated/xdg-shell-protocol.c"
# Optional flags allow an extracted original distro development package locally;
# CI uses ordinary pkg-config for its installed upstream libwayland-client.
read -r -a client_cflags <<< "${TRAMES_WAYLAND_CFLAGS:-$(pkg-config --cflags wayland-client)}"
read -r -a client_libs <<< "${TRAMES_WAYLAND_LIBS:-$(pkg-config --libs wayland-client)}"
"${CC:-cc}" -O2 -Wall -Wextra -Werror "${client_cflags[@]}" -I"$output/generated" \
    tests/fixtures/wayland-client.c "$output/generated/xdg-shell-protocol.c" \
    "${client_libs[@]}" -o "$output/client-native"
TRAMES_WAYLAND_C_CLIENT="$output/client-native" cargo test -p trames-wayland --locked --test native_client -- --ignored --nocapture
