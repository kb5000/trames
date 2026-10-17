#!/usr/bin/env bash
# Original libwayland client ABI; no generated Rust client shares server code.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" == Darwin ]]; then
    export PKG_CONFIG_PATH="$(brew --prefix libffi)/lib/pkgconfig:$(brew --prefix expat)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    python3 scripts/build-wayland-client-apple.py
    export TRAMES_WAYLAND_SCANNER="$PWD/target/wayland/apple-client/wayland-scanner"
    export TRAMES_WAYLAND_CFLAGS="-I$PWD/target/wayland/apple-client/generated -I$PWD/target/wayland/apple-client/original/wayland-1.23.1/src"
    export TRAMES_WAYLAND_LIBS="-L$PWD/target/wayland/apple-client -lwayland-client"
fi
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
text_xml="$(dirname "$(dirname "$(dirname "$xml")")")/unstable/text-input/text-input-unstable-v3.xml"
"$scanner" client-header "$text_xml" "$output/generated/text-input-v3-client-protocol.h"
"$scanner" private-code "$text_xml" "$output/generated/text-input-v3-protocol.c"
v1_xml="$(dirname "$text_xml")/text-input-unstable-v1.xml"
"$scanner" client-header "$v1_xml" "$output/generated/text-input-v1-client-protocol.h"
"$scanner" private-code "$v1_xml" "$output/generated/text-input-v1-protocol.c"
# Optional flags allow an extracted original distro development package locally;
# CI uses ordinary pkg-config for its installed upstream libwayland-client.
read -r -a client_cflags <<< "${TRAMES_WAYLAND_CFLAGS:-$(pkg-config --cflags wayland-client)}"
read -r -a client_libs <<< "${TRAMES_WAYLAND_LIBS:-$(pkg-config --libs wayland-client)}"
"${CC:-cc}" -O2 -Wall -Wextra -Werror "${client_cflags[@]}" -I"$output/generated" \
    tests/fixtures/wayland-client.c "$output/generated/xdg-shell-protocol.c" \
    "${client_libs[@]}" -o "$output/client-native"
"${CC:-cc}" -O2 -Wall -Wextra -Werror "${client_cflags[@]}" -I"$output/generated" \
    tests/fixtures/wayland-scene.c "$output/generated/xdg-shell-protocol.c" \
    "${client_libs[@]}" -o "$output/scene-native"
"${CC:-cc}" -O2 -Wall -Wextra -Werror "${client_cflags[@]}" -I"$output/generated" \
    tests/fixtures/wayland-text-v3.c "$output/generated/xdg-shell-protocol.c" "$output/generated/text-input-v3-protocol.c" \
    "${client_libs[@]}" -o "$output/text-v3-native"
"${CC:-cc}" -O2 -Wall -Wextra -Werror "${client_cflags[@]}" -I"$output/generated" \
    tests/fixtures/wayland-text-v1.c "$output/generated/xdg-shell-protocol.c" "$output/generated/text-input-v1-protocol.c" \
    "${client_libs[@]}" -o "$output/text-v1-native"
TRAMES_WAYLAND_TEXT_V1_CLIENT="$output/text-v1-native" TRAMES_WAYLAND_C_CLIENT="$output/client-native" TRAMES_WAYLAND_SCENE_CLIENT="$output/scene-native" TRAMES_WAYLAND_TEXT_V3_CLIENT="$output/text-v3-native" cargo test -p trames-wayland --locked --test native_client -- --ignored --nocapture
