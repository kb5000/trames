#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $(uname -s) != Darwin ]]; then
  echo 'iOS linking requires macOS with Xcode and the Apple SDK.' >&2
  exit 1
fi
platform=${1:-simulator}
export IPHONEOS_DEPLOYMENT_TARGET=17.0
case "$platform" in
  simulator) rust_target=aarch64-apple-ios-sim; sdk=iphonesimulator; configuration=Debug; destination='generic/platform=iOS Simulator' ;;
  device) rust_target=aarch64-apple-ios; sdk=iphoneos; configuration=Release; destination='generic/platform=iOS' ;;
  *) echo 'usage: scripts/build-ios.sh [simulator|device]' >&2; exit 2 ;;
esac
# The guest ELF is data, built by the Linux prerequisite job, never linked by Xcode.
[[ -f target/musl-hybrid/libc.so && -f target/musl-hybrid/manifest.json ]]
mkdir -p apps/ios/Trames/GuestMusl
cp target/musl-hybrid/libc.so target/musl-hybrid/manifest.json target/musl-hybrid/musl-COPYRIGHT.txt target/musl-hybrid/GPL-3.0.txt apps/ios/Trames/GuestMusl/
[[ -f target/vulkan-guest/libtrames_vulkan_icd.so && -f target/vulkan-guest/manifest.json ]]
mkdir -p apps/ios/Trames/GuestVulkan
cp target/vulkan-guest/libtrames_vulkan_icd.so target/vulkan-guest/manifest.json target/vulkan-guest/GPL-3.0.txt apps/ios/Trames/GuestVulkan/
if [[ -f target/vulkan-guest/libEGL.so.1 ]]; then
  cp target/vulkan-guest/libEGL.so.1 target/vulkan-guest/egl-manifest.json apps/ios/Trames/GuestVulkan/
fi
cp -R target/vulkan-guest/acceptance apps/ios/Trames/GuestVulkan/
cp -R target/vulkan-guest/wayland-acceptance apps/ios/Trames/GuestVulkan/
if [[ -d target/vulkan-guest/Notices ]]; then cp -R target/vulkan-guest/Notices apps/ios/Trames/GuestVulkan/; fi
python3 - <<'PYCODE'
import hashlib,json,pathlib
p=pathlib.Path('apps/ios/Trames/GuestVulkan');m=json.loads((p/'manifest.json').read_text());assert hashlib.sha256((p/'libtrames_vulkan_icd.so').read_bytes()).hexdigest()==m['icd_sha256']
if (p/'libEGL.so.1').exists():
    e=json.loads((p/'egl-manifest.json').read_text());assert hashlib.sha256((p/'libEGL.so.1').read_bytes()).hexdigest()==e['egl_sha256']
PYCODE
python3 scripts/prepare-vulkan.py --apple
mvk="$PWD/target/vulkan-upstream/moltenvk-all/MoltenVK/MoltenVK/static/MoltenVK.xcframework"
case "$platform" in
  device) export TRAMES_MOLTENVK_STATIC_DIR="$mvk/ios-arm64" ;;
  simulator) export TRAMES_MOLTENVK_STATIC_DIR="$mvk/ios-arm64_x86_64-simulator" ;;
esac
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
xcodebuild -quiet -project apps/ios/Trames.xcodeproj -scheme Trames -configuration "$configuration" \
  -destination "$destination" -derivedDataPath "apps/ios/build/$platform" \
  CODE_SIGNING_ALLOWED=NO \
  TRAMES_RUST_LIB_DIR="$TRAMES_RUST_LIB_DIR" -skipPackagePluginValidation build

app_bundle="apps/ios/build/$platform/Build/Products/$configuration-$sdk/Trames.app"
python3 scripts/finalize-ios-app.py "$app_bundle"
app_binary="$app_bundle/Trames"
if otool -L "$app_binary" | grep -F 'libtrames_ffi.dylib'; then
  echo 'The app must link the Rust static archive.' >&2
  exit 1
fi
