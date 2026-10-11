#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p target/cpp-sanitizers
for dispatch in computed portable; do
    flags=()
    if [[ "$dispatch" == portable ]]; then flags+=(-DTRAMES_PORTABLE_DISPATCH); fi
    "${CXX:-c++}" -std=c++17 -O1 -g -fno-omit-frame-pointer -fno-exceptions -fno-rtti \
        -fsanitize=address,undefined -Wall -Wextra -Werror "${flags[@]}" \
        native/interpreter-cpp/engine.cpp native/interpreter-cpp/sanitizer_test.cpp \
        -o "target/cpp-sanitizers/$dispatch"
    "target/cpp-sanitizers/$dispatch" > "target/cpp-sanitizers/$dispatch.txt"
done
cmp target/cpp-sanitizers/computed.txt target/cpp-sanitizers/portable.txt
cat target/cpp-sanitizers/computed.txt
