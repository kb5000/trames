#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Packet-stack guest ABI comparisons and genuine Alpine TLS/apk over the gateway."""
import importlib.util, os, pathlib, shutil, socket, subprocess, tempfile, time
ROOT=pathlib.Path(__file__).resolve().parents[2]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
n=module('native_network',ROOT/'tests/integration/network.py')
m=module('m3',ROOT/'tests/integration/m3.py')
RUNNER=ROOT/'target/release/trames-run'
count=0

def same(actual,oracle):
    global count
    assert actual==oracle,(actual,oracle);count+=1

def main():
    start=time.monotonic()
    with tempfile.TemporaryDirectory(prefix='trames-virtual-network-') as d:
        tmp=pathlib.Path(d)
        for backend in ('memory','disk'):
            base=[RUNNER,*(['--memory'] if backend=='memory' else ['--storage',tmp/backend]),'--virtual-network','2','--max-instructions','200000000']
            for ipv6 in (False,True):
                family='6' if ipv6 else '4';vip='2001:db8::1' if ipv6 else '198.18.0.1';host='::1' if ipv6 else '127.0.0.1'
                external=[*base,'--host-map',f'{vip}={host}','--env',f'TRAMES_TEST_PEER={vip}',n.FIXTURE]
                oracle=[n.QEMU,'-cpu','cortex-a53',n.FIXTURE]
                for mode,kind in [('udp',socket.SOCK_DGRAM),('tcp',socket.SOCK_STREAM)]:
                    with n.echo_server(kind,ipv6) as port:a=n.result([*external,mode,port,family])
                    with n.echo_server(kind,ipv6) as port:b=n.result([*oracle,mode,port,family])
                    same(a,b)
                    if mode=='udp':
                        with n.echo_server(kind,ipv6) as port:n.result([*external,'udp-preflight',port,family])
                with socket.socket(socket.AF_INET6 if ipv6 else socket.AF_INET,socket.SOCK_STREAM) as reserved:
                    reserved.bind((host,0));port=reserved.getsockname()[1]
                    for mode in ('refused','refused-blocking'):same(n.result([*external,mode,port,family]),n.result([*oracle,mode,port,family]))
                same(n.result([*base,n.FIXTURE,'fork-server',0,family]),n.result([*oracle,'fork-server',0,family]))
            for mode in ('eintr','restart'):same(n.result([*base,n.FIXTURE,mode]),n.result([n.QEMU,'-cpu','cortex-a53',n.FIXTURE,mode]))
            got=n.result([*base,ROOT/'tests/fixtures/bin/virtual-network'])
            assert got==(0,b'virtual interfaces, musl DNS A/AAAA and ICMP echo: ok\n',b''),got
        print(f'Virtual TCP/UDP/refusal/fork/signals: {count} QEMU comparisons; guest interfaces, DNS and ICMP passed',flush=True)
        good=tmp/'gateway.pem';key=tmp/'gateway.key';bad=tmp/'bad.pem'
        for cert,private in [(good,key),(bad,tmp/'bad.key')]:
            subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-keyout',str(private),'-out',str(cert),'-subj','/CN=198.18.0.1','-addext','subjectAltName=DNS:198.18.0.1,DNS:127.0.0.1,IP:198.18.0.1,IP:127.0.0.1','-addext','basicConstraints=critical,CA:TRUE'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        index=(m.BUILD/'repo/aarch64/APKINDEX.tar.gz').read_bytes();prefix=tmp/'oracle';shutil.copytree(m.BUILD/'rootfs',prefix,symlinks=True)
        with m.server(good,key,index) as (url,paths,_):
            repositories=tmp/'repositories';repositories.write_text(url.replace('127.0.0.1','198.18.0.1')+'\n');(prefix/'etc/apk/repositories').write_text(url+'\n')
            store=tmp/'alpine';settings=['--repositories-file','/test-repositories','--no-progress']
            base=[RUNNER,'--storage',store,'--virtual-network','2','--host-map','198.18.0.1=127.0.0.1','--max-instructions','1000000000']
            def guest(program,*args,initialize=False,ca=good):
                imports=['--rootfs',m.ARCHIVE,'--file',f'/test-ca.pem={ca}','--file',f'/test-repositories={repositories}'] if initialize else []
                if ca!=good:imports+=['--file',f'/untrusted-ca.pem={ca}']
                return [*base,*imports,'--env','SSL_CERT_FILE='+('/test-ca.pem' if ca==good else '/untrusted-ca.pem'),'--guest',program,*args]
            a=m.run(guest('/sbin/apk',*settings,'update',initialize=True));b=m.run(m.oracle(prefix,'/sbin/apk','--root',str(prefix),'--no-progress','update',env={'SSL_CERT_FILE':str(good)}));same((a[0],a[1].replace(b'198.18.0.1',b'127.0.0.1'),a[2]),b)
            a=m.run(guest('/sbin/apk',*settings,'add','tree=2.2.1-r0'));b=m.run(m.oracle(prefix,'/sbin/apk','--root',str(prefix),'--no-progress','--no-scripts','add','tree=2.2.1-r0',env={'SSL_CERT_FILE':str(good)}));import re
            same((a[0],re.sub(rb'Executing busybox-[^\n]+\n',b'',a[1]),a[2]),b);same(m.run(guest('/usr/bin/tree','--version')),m.run(m.oracle(prefix,'/usr/bin/tree','--version')))
            failed=m.run(guest('/sbin/apk',*settings,'--force-refresh','update',ca=bad),None);assert failed[0]!=0 and b'certificate verify failed' in failed[2],failed
            assert '/aarch64/APKINDEX.tar.gz' in paths and '/aarch64/tree-2.2.1-r0.apk' in paths
            print('Actual dynamic Alpine HTTPS apk update/add and CA rejection through packet gateway passed',flush=True)
    print(f'Virtual network passed: {count} QEMU comparisons plus ABI/ICMP/DNS/TLS rejection checks; {time.monotonic()-start:.1f}s',flush=True)
if __name__=='__main__':main()
