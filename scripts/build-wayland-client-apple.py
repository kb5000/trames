#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original Wayland C client/scanner with a small Darwin OS adapter for tests.

No protocol, object dispatch, marshalling, queue or client implementation changes.
Linux acceptance uses the unchanged distro library; this is an Apple-only peer.
"""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'target/wayland/apple-client'
VERSION='1.23.1'
DIGEST='864fb2a8399e2d0ec39d56e9d9b753c093775beadc6022ce81f441929a81e5ed'
assert sys.platform=='darwin', 'Only Apple test peers need this OS adapter'
OUT.mkdir(parents=True,exist_ok=True)
archive=OUT/f'wayland-{VERSION}.tar.xz'
if not archive.exists():
    with urllib.request.urlopen(f'https://distfiles.alpinelinux.org/distfiles/v3.22/{archive.name}',timeout=60) as r:
        archive.write_bytes(r.read())
assert hashlib.sha256(archive.read_bytes()).hexdigest()==DIGEST, 'upstream source hash mismatch'
with tarfile.open(archive) as t:
    t.extractall(OUT/'original',filter='data')
source=OUT/'original'/f'wayland-{VERSION}'
config=OUT/'generated'
config.mkdir(exist_ok=True)
(config/'config.h').write_text('#define PACKAGE "wayland"\n#define PACKAGE_VERSION "1.23.1"\n#define HAVE_STRNDUP 1\n#define HAVE_BROKEN_MSG_CMSG_CLOEXEC 1\n#include <sys/socket.h>\n#ifndef MSG_NOSIGNAL\n#define MSG_NOSIGNAL 0\n#endif\n')
(OUT/'config.h').write_bytes((config/'config.h').read_bytes())
(config/'wayland-version.h').write_text('#define WAYLAND_VERSION_MAJOR 1\n#define WAYLAND_VERSION_MINOR 23\n#define WAYLAND_VERSION_MICRO 1\n#define WAYLAND_VERSION "1.23.1"\n')
# Retain the complete original file; change only OS functions not available on
# Darwin. epoll is server-only, so it is omitted from this client-only build.
os_source=(source/'src/wayland-os.c').read_text()
os_source=os_source.replace('#include <sys/epoll.h>','/* Darwin client-only build: no server epoll integration. */')
os_source=os_source.replace('int (*wl_epoll_create1)(int flags) = epoll_create1;','')
begin=os_source.index('int\nwl_os_epoll_create_cloexec(void)')
end=os_source.index('int\nwl_os_accept_cloexec',begin)
os_source=os_source[:begin]+os_source[end:]
begin=os_source.index('\tfd = wl_socket(domain, type | SOCK_CLOEXEC, protocol);')
end=os_source.index('\n\tfd = wl_socket(domain, type, protocol);',begin)
os_source=os_source[:begin]+os_source[end:]
os_source=os_source.replace('fd = wl_socket(domain, type, protocol);', 'fd = wl_socket(domain, type, protocol);\n\tif (fd >= 0) { int enabled = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled); }')
os_source=os_source.replace('#error "Don\'t know how to read ucred on this platform"', '''int wl_os_socket_peercred(int fd, uid_t *uid, gid_t *gid, pid_t *pid) {
    *pid = 0; return getpeereid(fd, uid, gid);
}''')
(config/'wayland-os.c').write_text(os_source)
(OUT/'DARWIN-PATCHES.md').write_text('Wayland 1.23.1 SHA256 '+DIGEST+'\n\nOriginal complete sources and COPYING retained under original/.\nOnly generated/wayland-os.c is changed: omit unused server epoll helpers; use existing fcntl CLOEXEC fallback for socket creation; getpeereid for peer credentials; original recvmsg CLOEXEC fallback configured. Protocol and client algorithms are unchanged. This library is a test peer and is not linked into the app.\n')

def flags(name):
    return shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs',name],text=True))
cc=shlex.split(os.environ.get('CC','cc'))
includes=['-include',str(config/'config.h'),'-I'+str(config),'-I'+str(source/'src')]
subprocess.run([*cc,'-O2',*includes,str(source/'src/scanner.c'),str(source/'src/wayland-util.c'),*flags('expat'),'-o',str(OUT/'wayland-scanner')],check=True)
for command,name in [('client-header','wayland-client-protocol.h'),('client-header','wayland-client-protocol-core.h'),('public-code','wayland-protocol.c')]:
    subprocess.run([str(OUT/'wayland-scanner'),command,str(source/'protocol/wayland.xml'),str(config/name)],check=True)
subprocess.run([*cc,'-O2','-dynamiclib','-fPIC',*includes,
    *map(str,[source/'src/wayland-client.c',source/'src/connection.c',source/'src/wayland-util.c',config/'wayland-os.c',config/'wayland-protocol.c']),
    *flags('libffi'),'-install_name',str(OUT/'libwayland-client.dylib'),'-o',str(OUT/'libwayland-client.dylib')],check=True)
print('Original C Wayland client/scanner ready:',OUT)
