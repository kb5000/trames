# SPDX-License-Identifier: GPL-3.0-or-later
set -eux
mount -t tmpfs -o mode=755 tmpfs /run
mkdir -p /run/docker /dev/shm
mount -t tmpfs -o size=16m,mode=1777 tmpfs /dev/shm
cat >/tmp/containerd.toml <<'TOML'
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
    grep -q "$2" "$1" && return 0
    kill -0 "$3" 2>/dev/null || return 1
    sleep 1
  done
  return 1
}
start_daemon() {
  # This acceptance isolates storage; default bridge/NAT has separate evidence.
  dockerd --containerd=/run/containerd/containerd.sock --storage-driver=overlay2 \
    --iptables=false --ip6tables=false --bridge=none --ip-forward=false --ip-masq=false \
    --add-runtime crun=/usr/bin/crun --default-runtime=crun >/tmp/dockerd.log 2>&1 &
  daemon=$!
  ready /tmp/dockerd.log 'API listen on' "$daemon"
  test "$(docker info --format '{{.Driver}}')" = overlay2
}
ready /tmp/containerd.log 'containerd successfully booted' "$containerd_pid"
start_daemon
echo DOCKER_OVERLAY_PROBE_PASS
if ! docker image inspect trames-overlay:base >/dev/null 2>&1; then
  docker import /tmp/alpine-rootfs.tar trames-overlay:base
fi
# Start with ordinary namespaces, seccomp and device policy. Retain the writable
# layer for commit; no privileged mode or host runtime is involved.
if ! docker inspect overlay-a >/dev/null 2>&1; then
docker run --name overlay-a --network none --ulimit nofile=256:256 trames-overlay:base \
  /bin/sh -exc 'test "$$" = 1; printf changed >/etc/hostname; rm /etc/alpine-release;
  mkdir /new; printf child >/new/child; ln /new/child /hard; ln -s /new/child /sym;
  chmod 600 /new/child; echo OVERLAY_PAYLOAD'
fi
test "$(docker inspect overlay-a --format '{{.State.ExitCode}}')" = 0
upper=$(docker inspect overlay-a --format '{{.GraphDriver.Data.UpperDir}}')
test -f "$upper/new/child"
test ! -e "$upper/bin/busybox"
echo DOCKER_OVERLAY_COPY_UP_PASS
docker diff overlay-a >/tmp/diff.log
cat /tmp/diff.log
grep -q 'D /etc/alpine-release' /tmp/diff.log
grep -q 'A /new' /tmp/diff.log
if ! docker image inspect trames-overlay:changed >/dev/null 2>&1; then
  docker commit overlay-a trames-overlay:changed
fi
# A new image layer must preserve hard links, symlinks and deletion whiteouts.
docker run --rm --network none --ulimit nofile=256:256 trames-overlay:changed \
  /bin/sh -exc 'test ! -e /etc/alpine-release; test "$(cat /new/child)" = child;
  test "$(stat -c %i /hard)" = "$(stat -c %i /new/child)";
  test "$(readlink /sym)" = /new/child; echo OVERLAY_COMMIT_PAYLOAD'
echo DOCKER_OVERLAY_COMMIT_PASS
# Another container using the original image cannot see the previous writes.
docker run --rm --network none --ulimit nofile=256:256 trames-overlay:base \
  /bin/sh -exc 'test -f /etc/alpine-release; test ! -e /new; echo OVERLAY_ISOLATED_PAYLOAD'
echo DOCKER_OVERLAY_ISOLATION_PASS
kill "$daemon"; wait "$daemon"; daemon=
cp /tmp/dockerd.log /tmp/dockerd-before-restart.log
start_daemon
docker run --rm --network none --ulimit nofile=256:256 trames-overlay:changed \
  /bin/sh -exc 'test ! -e /etc/alpine-release; test "$(cat /new/child)" = child;
  echo OVERLAY_RESTART_PAYLOAD'
echo DOCKER_OVERLAY_RESTART_PASS
docker rm overlay-a
docker image rm trames-overlay:changed trames-overlay:base
echo DOCKER_OVERLAY_CLEANUP_PASS
