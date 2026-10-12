#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unmodified Alpine dynamic userspace, authenticated TLS and signed apk operations."""
import contextlib, gzip, hashlib, http.server, io, os, pathlib, re, shutil, ssl, subprocess, tarfile, tempfile, threading, time, zlib
ROOT=pathlib.Path(__file__).resolve().parents[2]
RUNNER=ROOT/'target/release/trames-run'
BUILD=ROOT/'target/alpine-build'
ARCHIVE=pathlib.Path(os.getenv('TRAMES_ALPINE_CACHE',str(ROOT/'target/alpine-download')))/'alpine-minirootfs-3.22.6-aarch64.tar.gz'
QEMU=os.getenv('QEMU_AARCH64','qemu-aarch64')
ENV={'PATH':os.environ.get('PATH','/usr/bin:/bin'),'LANG':'C','LC_ALL':'C'}
comparisons=0

def run(command,expected=0,env=None,cwd=None):
    p=subprocess.run(list(map(str,command)),capture_output=True,timeout=300,env=env or ENV,cwd=cwd)
    if expected is not None and p.returncode!=expected:raise AssertionError((command,p.returncode,p.stdout[-2000:],p.stderr[-4000:]))
    return p.returncode,p.stdout,p.stderr

def equal(a,b):
    global comparisons
    if a!=b:raise AssertionError((a,b))
    comparisons+=1

def guest(store,*args,imports=(),env=(),initialize=False,memory=False):
    cmd=[RUNNER,'--max-instructions','1000000000']
    cmd+=['--memory'] if memory else ['--storage',store]
    if initialize:cmd+=['--rootfs',ARCHIVE]
    for target,source in imports:cmd+=['--file',f'{target}={source}']
    for value in env:cmd+=['--env',value]
    return cmd+['--guest',*args]

def oracle(prefix,program,*args,env=None):
    cmd=[QEMU,'-cpu','cortex-a53','-L',prefix]
    for key,value in (env or {}).items():cmd+=['-E',f'{key}={value}']
    return cmd+[prefix/program.lstrip('/'),*args]

def cert(folder,name,host='127.0.0.1'):
    key=folder/f'{name}.key';pem=folder/f'{name}.pem'
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-keyout',str(key),'-out',str(pem),'-subj',f'/CN={host}','-addext',f'subjectAltName=DNS:{host}'+(',IP:127.0.0.1' if host=='127.0.0.1' else ''),'-addext','basicConstraints=critical,CA:TRUE'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=ENV)
    return pem,key

def corrupt_signature(data):
    d=zlib.decompressobj(31);first=d.decompress(data)+d.flush();rest=d.unused_data
    # The signature is the first tar member, with its content immediately after
    # the valid header. Keep gzip and tar structure valid to test authentication.
    archive=tarfile.open(fileobj=io.BytesIO(first),mode='r:',ignore_zeros=True)
    signature=next(m for m in archive if m.name.startswith('.SIGN.RSA.') or m.name.startswith('./.SIGN.RSA.'))
    assert signature.isfile() and signature.size>=128
    first=bytearray(first);first[signature.offset_data]^=0x80
    return gzip.compress(bytes(first),mtime=0)+rest

@contextlib.contextmanager
def server(certificate,key,index):
    paths=[];payloads={'/aarch64/APKINDEX.tar.gz':index,'/aarch64/tree-2.2.1-r0.apk':(BUILD/'repo/aarch64/tree-2.2.1-r0.apk').read_bytes()}
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def do_GET(self):
            paths.append(self.path);data=payloads.get(self.path)
            if data is None:self.send_error(404);return
            self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    s=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(certificate,key);s.socket=ctx.wrap_socket(s.socket,server_side=True)
    t=threading.Thread(target=s.serve_forever,daemon=True);t.start()
    try:yield f'https://127.0.0.1:{s.server_port}',paths,payloads
    finally:s.shutdown();s.server_close();t.join(timeout=3)

def main():
    assert hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()=='821565fa8f3953eefd12497b166b4b50add2f7c57fb312e75862f5867e06fefe'
    start=time.monotonic()
    with tempfile.TemporaryDirectory(prefix='trames-m3-') as folder:
        tmp=pathlib.Path(folder);prefix=tmp/'oracle';shutil.copytree(BUILD/'rootfs',prefix,symlinks=True)
        (prefix/'test').mkdir();shutil.copy(ROOT/'tests/fixtures/bin/libm3probe.so',prefix/'test/libm3probe.so')
        for kind in ['pie','exec']:
            fixture=ROOT/f'tests/fixtures/bin/m3-{kind}';shutil.copy(fixture,prefix/f'test/m3-{kind}')
            for mode in ['dynamic','mapping','links','locks']:
                b=run(oracle(prefix,f'/test/m3-{kind}',mode),cwd=prefix/'tmp')
                for memory in [True,False]:
                    a=run([RUNNER,'--max-instructions','100000000',*(['--memory'] if memory else ['--storage',tmp/f'{kind}-{mode}']),'--rootfs',ARCHIVE,'--file',f'/test/libm3probe.so={ROOT}/tests/fixtures/bin/libm3probe.so',fixture,mode])
                    equal(a,b)
        print('Dynamic PIE/ET_EXEC, dlopen/TLS/fork and Linux ABI: 16 QEMU comparisons',flush=True)
        simple=[('/bin/busybox',['echo','dynamic Alpine']),('/bin/busybox',['printf','%s:%d\n','musl',42]),('/bin/busybox',['cat','/etc/alpine-release']),('/sbin/apk',['--version']),('/sbin/apk',['version','-t','2.1','2.0']),('/bin/busybox',['readlink','/lib/libc.musl-aarch64.so.1'])]
        for program,args in simple:
            b=run(oracle(prefix,program,*args))
            for memory in [True,False]:equal(run(guest(tmp/f'simple-{comparisons}',program,*args,initialize=True,memory=memory)),b)
        base=tmp/'base';run(guest(base,'/bin/busybox','true',initialize=True))
        good,key=cert(tmp,'good');badca,_=cert(tmp,'untrusted');wrong,wrongkey=cert(tmp,'wrong','wrong.example')
        index=(BUILD/'repo/aarch64/APKINDEX.tar.gz').read_bytes()
        with server(good,key,index) as (url,paths,payloads):
            repositories=tmp/'repositories';repositories.write_text(url+'\n')
            store=tmp/'installed';shutil.copytree(base,store)
            imports=[('/test-ca.pem',good),('/test-repositories',repositories)]
            settings=['--repositories-file','/test-repositories','--no-progress']
            a=run(guest(store,'/sbin/apk',*settings,'update',imports=imports,env=['SSL_CERT_FILE=/test-ca.pem']))
            assert not a[2] and b'distinct packages available' in a[1]
            # Independent QEMU apk executes the identical upstream signature and
            # TLS validation. Prefix/URL strings are normalized below.
            (prefix/'etc/apk/repositories').write_text(url+'\n')
            b=run(oracle(prefix,'/sbin/apk','--root',str(prefix),'--no-progress','update',env={'SSL_CERT_FILE':str(good)}))
            equal(a,b)
            a=run(guest(store,'/sbin/apk',*settings,'add','tree=2.2.1-r0',env=['SSL_CERT_FILE=/test-ca.pem']))
            assert not a[2] and b'Installing tree (2.2.1-r0)' in a[1] and b'Executing busybox-' in a[1]
            # Native QEMU has no guest chroot/exec supervisor; omit scripts only
            # for this oracle install, then compare the installed binary itself.
            b=run(oracle(prefix,'/sbin/apk','--root',str(prefix),'--no-progress','--no-scripts','add','tree=2.2.1-r0',env={'SSL_CERT_FILE':str(good)}))
            equal((a[0],re.sub(rb'Executing busybox-[^\n]+\n',b'',a[1]),a[2]),b)
            for args in [['--version'],['-fi','--noreport','/etc/apk/keys']]:
                equal(run(guest(store,'/usr/bin/tree',*args)),run(oracle(prefix,'/usr/bin/tree',*args)))
            assert '/aarch64/APKINDEX.tar.gz' in paths and '/aarch64/tree-2.2.1-r0.apk' in paths
            print('Verified HTTPS apk update/add, successful trigger and installed tree execution',flush=True)
            # Trust failure is distinct from package authentication failure.
            for label,ca,expected_text in [('untrusted',badca,b'certificate verify failed')]:
                rejected=tmp/label;shutil.copytree(base,rejected)
                a=run(guest(rejected,'/sbin/apk',*settings,'update',imports=[('/test-ca.pem',ca),('/test-repositories',repositories)],env=['SSL_CERT_FILE=/test-ca.pem']),None)
                b=run(oracle(prefix,'/sbin/apk','--root',str(prefix),'--no-progress','--force-refresh','update',env={'SSL_CERT_FILE':str(ca)}),None)
                assert a[0]!=0 and b[0]!=0 and expected_text in a[2] and expected_text in b[2];comparisons_add()
            signed_bad=tmp/'bad.apk';signed_bad.write_bytes(corrupt_signature(payloads['/aarch64/tree-2.2.1-r0.apk']))
            rejected=tmp/'bad-package';shutil.copytree(base,rejected)
            a=run(guest(rejected,'/sbin/apk','--repositories-file','/dev/null','--no-progress','--force-non-repository','add','/test-bad.apk',imports=[('/test-bad.apk',signed_bad)]),None)
            clean=tmp/'clean';shutil.copytree(BUILD/'rootfs',clean,symlinks=True)
            b=run(oracle(clean,'/sbin/apk','--root',str(clean),'--repositories-file','/dev/null','--no-progress','--force-non-repository','add',str(signed_bad)),None)
            assert a[0]!=0 and b[0]!=0 and b'BAD signature' in a[2] and b'BAD signature' in b[2],(a,b);comparisons_add()
            assert run(guest(rejected,'/sbin/apk','info','-e','tree'),1)[0]==1
            assert run(guest(rejected,'/bin/busybox','readlink','/usr/bin/tree'))==run(guest(base,'/bin/busybox','readlink','/usr/bin/tree'))
        for label,certificate,privatekey,data,text in [('hostname',wrong,wrongkey,index,b"doesn't match host"),('index',good,key,corrupt_signature(index),b'BAD signature')]:
            with server(certificate,privatekey,data) as (url,paths,_):
                repositories=tmp/f'{label}-repositories';repositories.write_text(url+'\n')
                rejected=tmp/label;shutil.copytree(base,rejected)
                a=run(guest(rejected,'/sbin/apk','--repositories-file','/test-repositories','--no-progress','update',imports=[('/test-ca.pem',certificate),('/test-repositories',repositories)],env=['SSL_CERT_FILE=/test-ca.pem']),None)
                (prefix/'etc/apk/repositories').write_text(url+'\n')
                b=run(oracle(prefix,'/sbin/apk','--root',str(prefix),'--no-progress','--force-refresh','update',env={'SSL_CERT_FILE':str(certificate)}),None)
                assert a[0]!=0 and b[0]!=0 and text in a[2] and text in b[2],(label,a,b);comparisons_add()
                if label=='hostname':assert not paths
        altered=tmp/'altered.tar.gz';data=bytearray(ARCHIVE.read_bytes());data[-10]^=1;altered.write_bytes(data)
        value=run([RUNNER,'--storage',tmp/'rejected-root','--rootfs',altered,'--guest','/bin/busybox','true'],125)
        assert b'SHA-256 mismatch' in value[2]
    print(f'M3 passed: {comparisons} QEMU comparisons; certificate, hostname, package/index authentication and archive rejection; {time.monotonic()-start:.1f}s',flush=True)

def comparisons_add():
    global comparisons
    comparisons+=1
if __name__=='__main__':main()
