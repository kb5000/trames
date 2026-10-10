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
export TRAMES_RUST_LIB_DIR="$PWD/target/$rust_target/release"
xcodegen generate --spec apps/ios/project.yml
xcodebuild -project apps/ios/Trames.xcodeproj -scheme Trames -configuration Debug \
  -sdk "$sdk" -destination "$destination" -derivedDataPath "apps/ios/build/$platform" \
  ARCHS=arm64 ONLY_ACTIVE_ARCH=YES CODE_SIGNING_ALLOWED=NO \
  TRAMES_RUST_LIB_DIR="$TRAMES_RUST_LIB_DIR" -skipPackagePluginValidation build
