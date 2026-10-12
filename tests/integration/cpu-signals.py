#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual musl signal handlers recover from traps and precise memory faults."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
fixture=ROOT/'tests/fixtures/bin/cpu-signals'
runner=os.getenv('TRAMES_RUNNER',str(ROOT/'target/debug/trames-run'))
actual=subprocess.run([runner,'--memory',str(fixture)],capture_output=True,timeout=30)
oracle=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','max',str(fixture)],capture_output=True,timeout=30)
assert (actual.returncode,actual.stdout,actual.stderr)==(0,b'cpu-signals-ok\n',b''),actual
assert (oracle.returncode,oracle.stdout,oracle.stderr)==(0,actual.stdout,b''),oracle
print('Actual musl handlers: BRK, UDF, HLT, unmapped load/fetch, write protection, ordered-access alignment and edited context passed')
