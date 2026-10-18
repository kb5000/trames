#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original signed guest Docker: mandatory daemon/image/create checks, run diagnostic.

No host Docker socket or custom runtime is used. A blocked start is recorded
separately; pass --require-run to require the actual container payload too.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import m6

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--require-run', action='store_true')
parser.add_argument('--privileged', action='store_true', help='Explicit guest privileged profile with allow-all device policy')
parser.add_argument('--isolated', action='store_true', help='Private network=none, IPC and cgroup namespaces, default Docker seccomp/device profile')
parser.add_argument('--internal-bridge', action='store_true', help='Original Docker bridge/veth/IP and root-to-container HTTP, with iptables/NAT explicitly disabled; keeps default IPC/cgroup/seccomp/device isolation')
parser.add_argument('--cpus', choices=('1','4','both'), default='both', help='Only run the requested CPU configuration')
args = parser.parse_args()
if (args.isolated or args.internal_bridge) and args.privileged:
    parser.error('--isolated verifies the ordinary default security profile; omit --privileged')
if args.internal_bridge:
    args.isolated = True
    args.require_run = True
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/('target/docker-bridge-lifecycle' if args.internal_bridge else 'target/docker-lifecycle')
OUT.mkdir(parents=True, exist_ok=True)
RUNNER = os.getenv('TRAMES_RUNNER', str(ROOT/'target/release/trames-run'))
script = r'''
set -eu
# These are ordinary guest mounts, fresh for this virtual kernel session.
mount -t tmpfs -o mode=755 tmpfs /run
mkdir -p /run/docker /dev/shm
mount -t tmpfs -o size=16m,mode=1777 tmpfs /dev/shm
cat > /tmp/containerd.toml <<'TOML'
version = 3
disabled_plugins = ["io.containerd.cri.v1.images", "io.containerd.cri.v1.runtime", "io.containerd.nri.v1.nri"]
TOML
containerd --config /tmp/containerd.toml >/tmp/containerd.log 2>&1 &
containerd_pid=$!
daemon=
cleanup() {
  if test -n "$daemon"; then kill "$daemon" 2>/dev/null || :; wait "$daemon" 2>/dev/null || :; fi
  kill "$containerd_pid" 2>/dev/null || :; wait "$containerd_pid" 2>/dev/null || :
  cat /tmp/containerd.log /tmp/dockerd.log 2>/dev/null || :
  dmesg
}
trap cleanup EXIT
ready() {
  for i in $(seq 1 180); do
    if grep -q "$2" "$1"; then return 0; fi
    kill -0 "$3" 2>/dev/null || return 1
    sleep 1
  done
  return 1
}
ready /tmp/containerd.log 'containerd successfully booted' "$containerd_pid"
dockerd --containerd=/run/containerd/containerd.sock --storage-driver=vfs \
  --iptables=false --ip6tables=false --bridge=none --ip-forward=false --ip-masq=false \
  --add-runtime crun=/usr/bin/crun --default-runtime=crun >/tmp/dockerd.log 2>&1 &
daemon=$!
ready /tmp/dockerd.log 'API listen on' "$daemon"
docker version
test "$(docker info --format '{{.Driver}} {{.CgroupDriver}} {{.CgroupVersion}} {{.DefaultRuntime}}')" = 'vfs cgroupfs 2 crun'
echo DOCKER_DAEMON_PASS
docker import /tmp/alpine-rootfs.tar trames-alpine:probe
docker image inspect trames-alpine:probe --format '{{.Id}} {{.Architecture}}'
echo DOCKER_IMAGE_PASS
docker create @PROFILE@ --ulimit nofile=256:256 --name smoke --network host --ipc host --cgroupns host \
  --security-opt seccomp=unconfined trames-alpine:probe /bin/sh -ec \
  'test "$$" = 1; id; hostname; echo DOCKER_GUEST_PAYLOAD; exit 7'
test "$(docker inspect smoke --format '{{.State.Status}}')" = created
echo DOCKER_CREATE_PASS
status=0
docker start -a smoke >/tmp/start.log 2>&1 || status=$?
cat /tmp/start.log
printf 'DOCKER_START_STATUS=%s\n' "$status"
docker inspect smoke --format 'container running={{.State.Running}} exit={{.State.ExitCode}} error={{.State.Error}}'
if grep -q '^DOCKER_GUEST_PAYLOAD$' /tmp/start.log; then
  test "$(docker inspect smoke --format '{{.State.ExitCode}}')" = 7
  docker logs smoke >/tmp/container.log 2>&1
  cat /tmp/container.log
  grep -q '^DOCKER_GUEST_PAYLOAD$' /tmp/container.log
  echo DOCKER_RUN_PASS
else
  echo DOCKER_RUN_BLOCKED
fi
docker rm -f smoke
docker image rm trames-alpine:probe
echo DOCKER_CLEANUP_PASS
'''
script = script.replace('@PROFILE@', '--privileged' if args.privileged else '')
if args.isolated:
    for old, new in [('--network host', '--network none'), ('--ipc host', ''),
                     ('--cgroupns host', ''), ('--security-opt seccomp=unconfined', '')]:
        assert script.count(old)==1, ('profile marker missing or duplicated',old)
        script=script.replace(old,new)
    assert '--network none' in script and not any(flag in script for flag in
        ('--network host','--ipc host','--cgroupns host','seccomp=unconfined','--privileged'))
    script=script.replace('docker create ','''root_net=$(readlink /proc/self/ns/net)
root_ipc=$(readlink /proc/self/ns/ipc)
root_cgroup=$(readlink /proc/self/ns/cgroup)
docker create --env ROOT_NET="$root_net" --env ROOT_IPC="$root_ipc" --env ROOT_CGROUP="$root_cgroup" ''',1)
    script=script.replace('test "$$" = 1; id; hostname;', '''test "$$" = 1;
  test "$(readlink /proc/self/ns/net)" != "$ROOT_NET";
  test "$(readlink /proc/self/ns/ipc)" != "$ROOT_IPC";
  test "$(readlink /proc/self/ns/cgroup)" != "$ROOT_CGROUP";
  test "$(cat /proc/self/cgroup)" = "0::/";
  grep -Eq "^Seccomp:[[:space:]]+2$" /proc/self/status;
  test "$(cat /proc/sys/net/ipv4/ip_unprivileged_port_start)" = 0;
  if ip link show eth0 2>/dev/null; then exit 91; fi;
  id; hostname;''')
if args.internal_bridge:
    script=script.replace('--bridge=none --ip-forward=false','--bridge=docker0 --ip-forward=true')
    script=script.replace('--network none','--network bridge')
    script=script.replace('if ip link show eth0 2>/dev/null; then exit 91; fi;', '''test -e /sys/class/net/eth0;
  ip -4 addr show dev eth0; ip route;
  ping -c 1 -W 2 172.17.0.1;''')
    script=script.replace('echo DOCKER_GUEST_PAYLOAD; exit 7',r'''echo DOCKER_GUEST_PAYLOAD;
  echo DOCKER_HTTP_READY;
  printf "HTTP/1.1 200 OK\r\nContent-Length: 21\r\nConnection: close\r\n\r\nTRAMES_DOCKER_BRIDGE\n" | nc -l -p 18080 -w 90;
  exit 7''')
    script=script.replace('docker start -a smoke >/tmp/start.log 2>&1 || status=$?', '''docker start -a smoke >/tmp/start.log 2>&1 &
start_pid=$!
ready /tmp/start.log '^DOCKER_HTTP_READY$' "$start_pid"
ip=$(docker inspect smoke --format '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}')
test -n "$ip"
for i in $(seq 1 10); do
  if wget -T 2 -qO /tmp/bridge-http "http://$ip:18080/"; then break; fi
  sleep 1
done
cat /tmp/bridge-http
grep -qx TRAMES_DOCKER_BRIDGE /tmp/bridge-http
echo DOCKER_BRIDGE_HTTP_PASS
wait "$start_pid" || status=$?''')
    script=script.replace('cat /tmp/containerd.log /tmp/dockerd.log', 'cat /tmp/start.log /tmp/container.log /tmp/containerd.log /tmp/dockerd.log')
    assert '--network bridge' in script and '--iptables=false' in script
results = {'profile': 'guest-privileged' if args.privileged else 'default-device-filter', 'gomaxprocs': 1, 'scope': 'Unmodified signed guest Docker/containerd/crun; mandatory daemon, vfs image import, create/inspect/remove. Container start is a separately recorded diagnostic until explicitly required.',
           'script': script, 'checks': []}
if args.isolated:
    results.update(profile='private-net-ipc-cgroup-default-seccomp',scope='Original Docker network=none with independent net/IPC/cgroup namespace identities, rebased cgroup mount/proc, applied network sysctls and enforced default seccomp/device filter. Bridge/NAT remain separate acceptance.')
if args.internal_bridge:
    results.update(profile='internal-bridge-default-seccomp',scope='Original Docker creates docker0, veth pair, netns, eth0/IP/gateway; container reaches bridge gateway and root guest fetches actual HTTP from the container. Default IPC/cgroup/seccomp/device isolation retained. Explicit iptables=false/ip6tables=false/ip-masq=false: no firewall/NAT/external DNS/port-publishing acceptance.')
with tempfile.TemporaryDirectory(prefix='trames-docker-') as directory:
    tmp = Path(directory)
    m6.repository(tmp/'repository', ('docker-engine', 'docker-cli', 'crun', 'libssl3'))
    # repository() includes alternative /bin/sh providers; keep Alpine BusyBox.
    packages = [p for p in sorted((tmp/'repository').rglob('*.apk'))
                if not p.name.startswith(('dash-', 'yash-'))]
    results['packages_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
    rootfs = m6.CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz'
    results['rootfs_sha256'] = hashlib.sha256(rootfs.read_bytes()).hexdigest()
    with gzip.open(rootfs, 'rb') as src, (tmp/'alpine.tar').open('wb') as dest:
        shutil.copyfileobj(src, dest)
    for cpus in (('1','4') if args.cpus=='both' else (args.cpus,)):
        base = [RUNNER, '--storage', str(tmp/('store-'+cpus)), '--network', 'full', '--cpus', cpus,
                '--env', 'GOMAXPROCS=1', '--max-instructions', '20000000000']
        imported = [arg for p in packages for arg in ('--file', '/tmp/'+p.name+'='+str(p))]
        p = subprocess.run([*base, '--rootfs', str(rootfs), *imported,
            '--file', '/tmp/alpine-rootfs.tar='+str(tmp/'alpine.tar'),
            '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress',
            'add', *['/tmp/'+p.name for p in packages]], capture_output=True, timeout=300)
        (OUT/f'install-{cpus}.log').write_bytes(p.stdout+p.stderr)
        assert p.returncode == 0 and b'ERROR:' not in p.stdout+p.stderr, p
        with (OUT/f'lifecycle-{cpus}.log').open('wb') as log:
            p = subprocess.run([*base, '--guest', '/bin/sh', '-c', script],
                               stdout=log, stderr=subprocess.STDOUT, timeout=900)
        output = (OUT/f'lifecycle-{cpus}.log').read_text()
        checks = {name: 'DOCKER_'+name+'_PASS\n' in output
                  for name in (('DAEMON', 'IMAGE', 'CREATE', 'RUN', 'CLEANUP', 'BRIDGE_HTTP') if args.internal_bridge else ('DAEMON', 'IMAGE', 'CREATE', 'RUN', 'CLEANUP'))}
        status = re.search(r'DOCKER_START_STATUS=(\d+)', output)
        results['checks'].append({'cpus': int(cpus), 'exit_code': p.returncode,
            'checks': checks, 'start_exit': int(status[1]) if status else None, 'output': output})
        (OUT/'results.json').write_text(json.dumps(results, indent=2)+'\n')
        assert p.returncode == 0 and all(checks[n] for n in ('DAEMON','IMAGE','CREATE','CLEANUP')), output[-12000:]
        if args.internal_bridge:
            assert checks['BRIDGE_HTTP'], output[-12000:]
        print(f'{cpus}-CPU original Docker: daemon/image/create/cleanup passed; container run: '+
              ('PASSED' if checks['RUN'] else 'BLOCKED (see recorded diagnostic)'), flush=True)
        if args.require_run:
            assert checks['RUN'], output[-12000:]
print(f'Evidence: {OUT}')
