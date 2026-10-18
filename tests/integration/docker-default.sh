# SPDX-License-Identifier: GPL-3.0-or-later
set -eux
# Ordinary boot mounts; no Docker/containerd configuration is installed.
mount -t tmpfs -o mode=755 tmpfs /run
mkdir -p /dev/shm
mount -t tmpfs -o size=64m,mode=1777 tmpfs /dev/shm
test ! -e /etc/docker/daemon.json
! command -v crun
test -z "${GOMAXPROCS+x}"
daemon=
cleanup() {
  if test -n "$daemon"; then
    kill "$daemon" 2>/dev/null || :
    wait "$daemon" 2>/dev/null || :
  fi
  cat /tmp/dockerd.log
  dmesg
}
trap cleanup EXIT
ready() {
  for i in $(seq 1 180); do
    grep -q "$2" "$1" && return 0
    kill -0 "$3" 2>/dev/null || return 1
    sleep 1
  done
  return 1
}
start_daemon() {
  dockerd >/tmp/dockerd.log 2>&1 &
  daemon=$!
  ready /tmp/dockerd.log 'API listen on' "$daemon"
  test "$(docker info --format '{{.DefaultRuntime}} {{.Driver}}')" = 'runc overlay2'
  test -S /run/docker/containerd/containerd.sock
  grep "^disabled_plugins" /run/docker/containerd/containerd.toml || :
}
start_daemon
echo DOCKER_DEFAULT_DAEMON_PASS
docker import /tmp/alpine-rootfs.tar trames-default:probe
docker run -d --name default-app -p @PUBLISHED@:8080 -v /runtime-probe:/runtime-probe:ro trames-default:probe /bin/sh -ec '
  test "$$" = 1
  test "$(stat -c %i /proc)" = 1
  grep -Eq "^Seccomp:[[:space:]]+2$" /proc/self/status
  /runtime-probe devices
  test "$(cat /proc/self/cgroup)" = "0::/"
  ip addr show dev eth0
  nslookup localhost
  wget -T 10 -qO /tmp/peer http://198.18.0.1:@HTTP@/
  grep -qx TRAMES_DEFAULT_EGRESS /tmp/peer
  printf persistent >/default-owned
  echo DEFAULT_CONTAINER_READY
  printf "HTTP/1.1 200 OK\r\nContent-Length: 23\r\nConnection: close\r\n\r\nTRAMES_DEFAULT_RUNC_OK\n" | nc -l -p 8080 -w 120
  sleep 3600'
for i in $(seq 1 180); do
  docker logs default-app >/tmp/container.log
  grep -qx DEFAULT_CONTAINER_READY /tmp/container.log && break
  test "$(docker inspect default-app --format '{{.State.Running}}')" = true
  sleep 1
done
cat /tmp/container.log
grep -qx DEFAULT_CONTAINER_READY /tmp/container.log
echo DOCKER_DEFAULT_PAYLOAD_PASS
echo HOST_DEFAULT_PUBLISHED_READY
docker exec default-app /bin/sh -ec 'test "$(cat /default-owned)" = persistent; test "$(stat -c %i /proc)" = 1; echo DEFAULT_EXEC_OK' >/tmp/exec.log
cat /tmp/exec.log
grep -qx DEFAULT_EXEC_OK /tmp/exec.log
echo DOCKER_DEFAULT_EXEC_PASS
docker stop -t 1 default-app
docker wait default-app
upper=$(docker inspect default-app --format '{{.GraphDriver.Data.UpperDir}}')
test "$(cat "$upper/default-owned")" = persistent
test ! -e "$upper/bin/busybox"
echo DOCKER_DEFAULT_OVERLAY_PASS
kill "$daemon"; wait "$daemon"; daemon=
cp /tmp/dockerd.log /tmp/dockerd-before-restart.log
start_daemon
test "$(docker inspect default-app --format '{{.State.Running}}')" = false
docker image inspect trames-default:probe >/dev/null
test "$(cat "$upper/default-owned")" = persistent
echo DOCKER_DEFAULT_RESTART_PASS
docker rm default-app
docker image rm trames-default:probe
echo DOCKER_DEFAULT_CLEANUP_PASS
