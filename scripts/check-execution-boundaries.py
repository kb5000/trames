#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reject interpreter or platform dependencies in the common Linux runtime."""
import json
import subprocess

metadata = json.loads(subprocess.check_output(
    ['cargo', 'metadata', '--format-version', '1', '--locked'], text=True))
names = {p['id']: p['name'] for p in metadata['packages']}
nodes = {n['id']: n for n in metadata['resolve']['nodes']}
ids = {p['name']: p['id'] for p in metadata['packages'] if p['id'] in metadata['workspace_members']}

def closure(name):
    pending, found = [ids[name]], set()
    while pending:
        identity = pending.pop()
        if identity in found:
            continue
        found.add(identity)
        pending.extend(d['pkg'] for d in nodes[identity]['deps']
                       if any(k['kind'] is None for k in d['dep_kinds']))
    return {names[i] for i in found}

for name in ['trames-kernel', 'trames-linux', 'trames-memory', 'trames-elf', 'trames-distro', 'trames-hvf', 'trames-mac']:
    forbidden = closure(name) & {'trames-cpu', 'trames-compute', 'trames-accel', 'trames-core', 'trames-ffi'}
    assert not forbidden, (name, 'depends on the interpreter/product:', sorted(forbidden))
assert closure('trames-execution') == {'trames-execution'}
for name in ['trames-kernel', 'trames-linux', 'trames-memory', 'trames-elf', 'trames-distro']:
    assert not closure(name) & {'trames-hvf', 'trames-mac'}, (name, 'depends on a hardware/product transport')
assert {'trames-cpu', 'trames-linux', 'trames-execution', 'trames-kernel'} <= closure('trames-core')
assert {'trames-hvf', 'trames-linux', 'trames-kernel'} <= closure('trames-mac')
assert not closure('trames-core') & {'trames-hvf', 'trames-mac'}
print('Execution boundaries: common kernel/Linux exclude interpreter and hardware transports; Mac excludes the interpreter; iPad excludes Hypervisor')
