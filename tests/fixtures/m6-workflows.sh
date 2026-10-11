#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
mkdir -p /root/m6
cd /root/m6
rm -rf project clone cmake-build gcc-app clang-app
mkdir project
cd project
git init -q
git config user.name 'Guest Developer'
git config user.email 'guest@example.invalid'
printf 'first\n' > history.txt
git add history.txt
git commit -q -m first
git checkout -q -b feature
printf 'second\n' >> history.txt
git commit -q -am second
git checkout -q master
git merge -q --ff-only feature
git gc
git fsck --full
cd ..
git clone -q project clone
test "$(git -C clone log --oneline | wc -l)" -eq 2
cmp project/history.txt clone/history.txt
printf 'git init/commit/branch/merge/gc/fsck/clone: ok\n'

cat > hello.c <<'EOF'
#include <stdio.h>
int main(void) { puts("compiled in guest: 42"); return 0; }
EOF
gcc -O2 -Wall -Wextra -Werror hello.c -o gcc-app
test "$(./gcc-app)" = 'compiled in guest: 42'
clang-19 -O2 -Wall -Wextra -Werror hello.c -o clang-app
test "$(./clang-app)" = 'compiled in guest: 42'
printf 'gcc/clang compile/link/execute: ok\n'

cat > Makefile <<'EOF'
all: make-gcc make-clang
make-gcc: hello.c
	$(CC) -O2 $< -o $@
make-clang: hello.c
	clang-19 -O2 $< -o $@
EOF
make -j2
test "$(./make-gcc)" = 'compiled in guest: 42'
test "$(./make-clang)" = 'compiled in guest: 42'
cat > CMakeLists.txt <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(GuestBuild C)
enable_testing()
add_executable(cmake-app hello.c)
add_test(NAME guest-binary COMMAND cmake-app)
EOF
cmake -S . -B cmake-build -DCMAKE_C_COMPILER=gcc
cmake --build cmake-build --parallel 2
ctest --test-dir cmake-build --output-on-failure
printf 'make parallel build, cmake configure/build/ctest: ok\n'

python3 /m6-python.py
printf 'retained M6 data\n' > /root/m6-retained
