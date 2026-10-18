#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original signed iptables: TCP/UDP source NAT, reversible hairpin and bridge DROP."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import m6
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cpus',choices=('1','4','both'),default='both')
args=parser.parse_args()
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'target/netfilter-nat';OUT.mkdir(parents=True,exist_ok=True)
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'));FIXTURE=ROOT/'tests/fixtures/bin/netfilter-nat'
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):self.send_response(200);self.end_headers();self.wfile.write(b'TRAMES_NATIVE_PEER\n')
    def log_message(self,*unused):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);udp.bind(('127.0.0.1',0))
def echo():
    while True:
        try:data,peer=udp.recvfrom(65535);udp.sendto(data,peer)
        except OSError:return
threading.Thread(target=echo,daemon=True).start()
results={'scope':__doc__,'inputs_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()for p in (FIXTURE,ROOT/'tests/fixtures/netfilter-nat.c',ROOT/'tests/fixtures/container-network.c')},'checks':[]}
try:
    with tempfile.TemporaryDirectory(prefix='trames-nat-')as directory:
        tmp=Path(directory);m6.repository(tmp/'repository',('iptables',));packages=[p for p in sorted((tmp/'repository').rglob('*.apk'))if not p.name.startswith(('dash-','yash-'))]
        results['packages_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in packages};rootfs=m6.CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz';results['rootfs_sha256']=hashlib.sha256(rootfs.read_bytes()).hexdigest()
        base=[RUNNER,'--storage',str(tmp/'store'),'--network','full','--max-instructions','1000000000'];imported=[arg for p in packages for arg in ('--file','/tmp/'+p.name+'='+str(p))]
        install=subprocess.run([*base,'--cpus','1','--rootfs',str(rootfs),*imported,'--guest','/sbin/apk','--repositories-file','/dev/null','--no-progress','add',*['/tmp/'+p.name for p in packages]],capture_output=True,timeout=120)
        (OUT/'install.log').write_bytes(install.stdout+install.stderr);assert install.returncode==0 and b'ERROR:'not in install.stdout+install.stderr,install
        for cpus in (('1','4')if args.cpus=='both'else(args.cpus,)):
            path='/netfilter-nat-'+cpus;p=subprocess.run([*base,'--cpus',cpus,'--host-map','198.18.0.1=127.0.0.1','--exec-file',path+'='+str(FIXTURE),'--guest',path,str(server.server_port),str(udp.getsockname()[1])],capture_output=True,timeout=120)
            output=p.stdout+p.stderr;(OUT/f'nat-{cpus}.log').write_bytes(output);checks={name:('NAT_'+name+'_PASS\n').encode()in output for name in ('UDP','TCP','HAIRPIN','BRIDGE_FILTER','LOOPBACK')}
            results['checks'].append({'cpus':int(cpus),'exit_code':p.returncode,'checks':checks,'output':output.decode(errors='replace')});(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n');assert p.returncode==0 and all(checks.values()),output[-10000:]
            print(f'{cpus}-CPU netfilter NAT: passed',flush=True)
finally:server.shutdown();udp.close()
print(f'Evidence: {OUT}')
