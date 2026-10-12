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
