# SPDX-License-Identifier: GPL-3.0-or-later
set -eux
mount -t tmpfs -o mode=755 tmpfs /run
mkdir -p /dev/shm
mount -t tmpfs -o size=64m,mode=1777 tmpfs /dev/shm
test ! -e /etc/docker/daemon.json
! command -v crun
! printenv GOMAXPROCS
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
start_daemon() {
  dockerd >/tmp/dockerd.log 2>&1 &
  daemon=$!
  for i in $(seq 1 180); do
    grep -q 'API listen on' /tmp/dockerd.log && break
    kill -0 "$daemon"
    sleep 1
  done
  grep 'API listen on' /tmp/dockerd.log
  test "$(docker info --format '{{.DefaultRuntime}} {{.Driver}}')" = 'runc overlay2'
  test -S /run/docker/containerd/containerd.sock
}
start_daemon
echo DOCKER_DEFAULT_DAEMON_PASS
docker import /tmp/alpine-rootfs.tar trames-default:probe
# Separate fresh VMs avoid coupling exec acceptance to published ports/binds.
# Every container keeps Docker's default runtime/network/security configuration.
docker run -d --name default-app trames-default:probe /bin/sh -ec '
  test "$$" = 1
  test "$(stat -c %i /proc)" = 1
  grep -Eq "^Seccomp:[[:space:]]+2$" /proc/self/status
  test "$(cat /proc/self/cgroup)" = "0::/"
  printf persistent >/default-owned
  echo DEFAULT_CONTAINER_READY
  while test ! -e /finish; do sleep 1; done
  sleep 5'
for i in $(seq 1 180); do
  docker logs default-app >/tmp/container.log
  grep -qx DEFAULT_CONTAINER_READY /tmp/container.log && break
  test "$(docker inspect default-app --format '{{.State.Running}}')" = true
  sleep 1
done
cat /tmp/container.log
grep -qx DEFAULT_CONTAINER_READY /tmp/container.log
echo DOCKER_DEFAULT_PAYLOAD_PASS
if test '@PART@' = exec; then
  docker exec default-app /bin/sh -ec 'test "$(cat /default-owned)" = persistent; test "$(stat -c %i /proc)" = 1; echo DEFAULT_EXEC_OK; touch /finish' >/tmp/exec.log
  cat /tmp/exec.log
  grep -qx DEFAULT_EXEC_OK /tmp/exec.log
  test "$(docker wait default-app)" = 0
  echo DOCKER_DEFAULT_EXEC_PASS
else
  docker stop -t 1 default-app
  docker wait default-app
  upper=$(docker inspect default-app --format '{{.GraphDriver.Data.UpperDir}}')
  test "$(cat "$upper/default-owned")" = persistent
  test ! -e "$upper/bin/busybox"
  echo DOCKER_DEFAULT_OVERLAY_PASS
  kill "$daemon"; wait "$daemon"; daemon=
  start_daemon
  test "$(docker inspect default-app --format '{{.State.Running}}')" = false
  docker image inspect trames-default:probe >/dev/null
  test "$(cat "$upper/default-owned")" = persistent
  echo DOCKER_DEFAULT_RESTART_PASS
fi
docker rm default-app
docker image rm trames-default:probe
echo DOCKER_DEFAULT_CLEANUP_PASS
