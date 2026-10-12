#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Optional one-shot baseline; measures the same guest binary with host opt-in toggled."""
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
ROOT=Path(__file__).resolve().parents[2]
runner=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
rows=[]
for workload in ['copy','copy-compare']:
    for length in [32,4096,65536]:
        modes={}
        for enabled in [False,True]:
            samples=[]
            for iteration in range(3):
                args=[runner,'--memory','--metrics','--max-instructions','2000000000']
                if enabled: args+=['--accelerator','pure']
                args+=[str(ROOT/'tests/fixtures/bin/accelerator'),str(length),workload]
                result=subprocess.run(args,capture_output=True,timeout=180)
                assert (result.returncode,result.stdout)==(0,b'accelerator-bench-ok\n'),result
                text=result.stderr.decode()
                runtime=json.loads(re.search(r'^trames metrics: (.+)$',text,re.M).group(1))
                native=json.loads(re.search(r'^trames native metrics: (.+)$',text,re.M).group(1))
                samples.append(dict(seconds=runtime['seconds'],instructions=runtime['instructions'],cpu_seconds=runtime['cpu_seconds'],native=native))
            modes[str(enabled)]=dict(samples=samples,median_seconds=statistics.median(s['seconds'] for s in samples),median_instructions=statistics.median(s['instructions'] for s in samples))
        row=dict(workload=workload,bytes_per_call=length,iterations=256,modes=modes,speedup=modes['False']['median_seconds']/modes['True']['median_seconds'])
        rows.append(row)
        print(workload,length,'end-to-end speedup',round(row['speedup'],2),flush=True)
report=dict(host=platform.platform(),host_cpu=platform.machine(),runner=runner,guest_arch='aarch64',description='Three runs per mode; medians. Whole guest run includes startup. ARM runner is itself executed by pinned QEMU; these are interpreter microbenchmarks, not physical-iPad or whole-application performance.',rows=rows)
(ROOT/'target/accelerator-benchmark.json').write_text(json.dumps(report,indent=2)+'\n')
