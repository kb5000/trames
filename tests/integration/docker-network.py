#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original guest Docker default firewall/NAT, DNS, published TCP/UDP and isolation.

Only controlled native HTTP/UDP peers run on the host. No host Docker, netns,
iptables or route mutation; all Docker tools are signed, unmodified Alpine APKs.
"""
import argparse
import gzip
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import m6

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cpus',choices=('1','4','both'),default='4')
args=parser.parse_args()
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'target/docker-full-network';OUT.mkdir(parents=True,exist_ok=True)
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
FIXTURE=ROOT/'tests/fixtures/bin/docker-network-probe'
SHELL=ROOT/'tests/integration/docker-network.sh'
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'TRAMES_FULL_NETWORK\n';self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def log_message(self,*unused):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);udp.bind(('127.0.0.1',0))
def echo():
    while True:
        try:data,peer=udp.recvfrom(65535);udp.sendto(data,peer)
        except OSError:return
threading.Thread(target=echo,daemon=True).start()
results={'scope':__doc__,'inputs_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()for p in (FIXTURE,SHELL)},'checks':[]}
try:
    with tempfile.TemporaryDirectory(prefix='trames-full-network-')as directory:
        tmp=Path(directory);m6.repository(tmp/'repository',('docker-engine','docker-cli','crun','libssl3'))
        packages=[p for p in sorted((tmp/'repository').rglob('*.apk'))if not p.name.startswith(('dash-','yash-'))]
        results['packages_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in packages}
        rootfs=m6.CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz';results['rootfs_sha256']=hashlib.sha256(rootfs.read_bytes()).hexdigest()
        with gzip.open(rootfs,'rb')as source,(tmp/'alpine.tar').open('wb')as dest:shutil.copyfileobj(source,dest)
        for cpus in (('1','4')if args.cpus=='both'else (args.cpus,)):
            # Reserve only ephemeral test peer ports, never touch host networking.
            with socket.socket()as candidate:candidate.bind(('127.0.0.1',0));published=candidate.getsockname()[1]
            script=SHELL.read_text().replace('@PUBLISHED@',str(published)).replace('@HTTP@',str(server.server_port)).replace('@UDP@',str(udp.getsockname()[1]))
            base=[RUNNER,'--storage',str(tmp/('store-'+cpus)),'--network','full','--cpus',cpus,'--env','GOMAXPROCS=1','--max-instructions','30000000000']
            imported=[arg for p in packages for arg in ('--file','/tmp/'+p.name+'='+str(p))]
            install=subprocess.run([*base,'--rootfs',str(rootfs),*imported,'--file','/tmp/alpine-rootfs.tar='+str(tmp/'alpine.tar'),'--exec-file','/network-probe='+str(FIXTURE),'--guest','/sbin/apk','--repositories-file','/dev/null','--no-progress','add',*['/tmp/'+p.name for p in packages]],capture_output=True,timeout=300)
            (OUT/f'install-{cpus}.log').write_bytes(install.stdout+install.stderr);assert install.returncode==0 and b'ERROR:'not in install.stdout+install.stderr,install
            logpath=OUT/f'network-{cpus}.log';host_result=[];stop=threading.Event()
            def browser():
                while not stop.wait(.05):
                    if logpath.exists()and '\nHOST_PUBLISHED_READY\n'in logpath.read_text(errors='replace'):break
                if stop.is_set():return
                try:
                    with socket.create_connection(('127.0.0.1',published),timeout=10)as connection:
                        connection.sendall(b'GET / HTTP/1.0\r\n\r\n');body=b''
                        while True:
                            chunk=connection.recv(4096)
                            if not chunk:break
                            body+=chunk
                        assert b'TRAMES_FULL_NETWORK\n'in body,body
                    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM)as client:
                        client.settimeout(10);client.sendto(b'HOST_UDP_PUBLISHED',('127.0.0.1',published));data,peer=client.recvfrom(4096);assert data==b'HOST_UDP_PUBLISHED'and peer[1]==published
                    host_result.append({'tcp':True,'udp':True})
                except Exception as error:host_result.append({'error':str(error)})
            worker=threading.Thread(target=browser,daemon=True);worker.start();started=time.monotonic()
            try:
                with logpath.open('wb')as log:process=subprocess.run([*base,'--host-map','198.18.0.1=127.0.0.1','--guest','/bin/sh','-ec',script],stdout=log,stderr=subprocess.STDOUT,timeout=900)
            finally:stop.set();worker.join(timeout=15)
            output=logpath.read_text();names=('DEFAULT_FIREWALL','EGRESS','PUBLISHED','EMBEDDED_DNS_HAIRPIN','USER_FILTER','BRIDGE_ISOLATION','INTERNAL','NETWORK_RESTART','NETWORK_CLEANUP')
            checks={n:('\nDOCKER_'+n+'_PASS\n')in output for n in names}
            results['checks'].append({'cpus':int(cpus),'exit_code':process.returncode,'seconds':round(time.monotonic()-started,3),'checks':checks,'host_browser':host_result,'output':output})
            (OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
            assert process.returncode==0 and all(checks.values())and host_result==[{'tcp':True,'udp':True}],output[-16000:]
            print(f'{cpus}-CPU original Docker full network: passed',flush=True)
finally:server.shutdown();udp.close()
print(f'Evidence: {OUT}')
