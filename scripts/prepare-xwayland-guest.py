#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Collect original Alpine-signed Xwayland/X11 clients for an offline probe.

The normal guest apk solver verifies signatures. No X server/library patches.
"""
import importlib.util
import os
import shutil
import tempfile
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('m6',ROOT/'tests/integration/m6.py')
m6=importlib.util.module_from_spec(spec);spec.loader.exec_module(m6)
out=ROOT/'target/xwayland'
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='trames-xwayland-') as temporary:
    repository=Path(temporary)/'repository'
    m6.repository(repository,('xwayland','xkeyboard-config','xterm','xclock','xeyes','xprop','font-dejavu','font-misc-misc'))
    shutil.copytree(repository,out/'repository',dirs_exist_ok=True)
subprocess.run(['bash',str(ROOT/'scripts/build-xwayland-launcher.sh')],check=True)
subprocess.run([os.getenv('MUSL_CC',str(ROOT/'target/busybox-build/musl/bin/musl-gcc')),
               '-O2','-Wall','-Wextra','-Werror',str(ROOT/'tests/fixtures/signal-leaf-stack.c'),
               '-o',str(out/'signal-leaf-stack')],check=True)
