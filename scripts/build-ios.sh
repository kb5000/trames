#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $(uname -s) != Darwin ]]; then
  echo 'iOS linking requires macOS with Xcode and the Apple SDK.' >&2
  exit 1
fi
platform=${1:-simulator}
case "$platform" in
  simulator) rust_target=aarch64-apple-ios-sim; sdk=iphonesimulator; destination='generic/platform=iOS Simulator' ;;
  device) rust_target=aarch64-apple-ios; sdk=iphoneos; destination='generic/platform=iOS' ;;
  *) echo 'usage: scripts/build-ios.sh [simulator|device]' >&2; exit 2 ;;
esac
# The guest ELF is data, built by the Linux prerequisite job, never linked by Xcode.
[[ -f target/musl-hybrid/libc.so && -f target/musl-hybrid/manifest.json ]]
mkdir -p apps/ios/Trames/GuestMusl
cp target/musl-hybrid/libc.so target/musl-hybrid/manifest.json target/musl-hybrid/musl-COPYRIGHT.txt target/musl-hybrid/GPL-3.0.txt apps/ios/Trames/GuestMusl/
python3 - <<'PYCODE'
import hashlib,json,pathlib
p=pathlib.Path('apps/ios/Trames/GuestMusl');m=json.loads((p/'manifest.json').read_text());assert hashlib.sha256((p/'libc.so').read_bytes()).hexdigest()==m['libc_sha256']
PYCODE
rustup target add "$rust_target"
cargo build -p trames-ffi --release --target "$rust_target" --locked
# Keep only the static archive in the app linker search directory. Cargo also
# produces a host-style dylib, which must never become an unembedded dependency.
mkdir -p "target/ios-link/$rust_target"
cp "target/$rust_target/release/libtrames_ffi.a" "target/ios-link/$rust_target/libtrames_ffi.a"
export TRAMES_RUST_LIB_DIR="$PWD/target/ios-link/$rust_target"
xcodegen generate --spec apps/ios/project.yml
xcodebuild -project apps/ios/Trames.xcodeproj -scheme Trames -configuration Debug \
  -destination "$destination" -derivedDataPath "apps/ios/build/$platform" \
  CODE_SIGNING_ALLOWED=NO \
  TRAMES_RUST_LIB_DIR="$TRAMES_RUST_LIB_DIR" -skipPackagePluginValidation build

app_binary="apps/ios/build/$platform/Build/Products/Debug-$sdk/Trames.app/Trames"
if otool -L "$app_binary" | grep -F 'libtrames_ffi.dylib'; then
  echo 'The app must link the Rust static archive.' >&2
  exit 1
fi
