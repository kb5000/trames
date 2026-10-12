#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the statically compiled ARM64 compute helpers under an independent CPU.
set -euo pipefail
cd "$(dirname "$0")/../.."
exec "${QEMU_AARCH64:-qemu-aarch64}" -cpu max target/aarch64-unknown-linux-musl/release/trames-run "$@"
