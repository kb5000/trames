# SPDX-License-Identifier: GPL-3.0-or-later
set -eux
mount -t tmpfs -o mode=755 tmpfs /run
mkdir -p /run/docker /dev/shm
mount -t tmpfs -o size=16m,mode=1777 tmpfs /dev/shm
printf 'nameserver 169.254.0.53\n' >/etc/resolv.conf
cat >/tmp/containerd.toml <<'TOML'
version = 3
disabled_plugins = ["io.containerd.cri.v1.images", "io.containerd.cri.v1.runtime", "io.containerd.nri.v1.nri"]
TOML
containerd --config /tmp/containerd.toml >/tmp/containerd.log 2>&1 &
containerd_pid=$!
daemon=
cleanup() {
  docker logs net-a 2>/dev/null || :
  docker logs net-b 2>/dev/null || :
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
ready /tmp/containerd.log 'containerd successfully booted' "$containerd_pid"
dockerd --containerd=/run/containerd/containerd.sock --storage-driver=vfs \
  --iptables=true --ip6tables=true --bridge=docker0 --ip-forward=true --ip-masq=true \
  --add-runtime crun=/usr/bin/crun --default-runtime=crun >/tmp/dockerd.log 2>&1 &
daemon=$!
ready /tmp/dockerd.log 'API listen on' "$daemon"
test "$(cat /sys/class/net/docker0/bridge/stp_state)" = 0
echo DOCKER_DEFAULT_FIREWALL_PASS
docker import /tmp/alpine-rootfs.tar trames-net:probe
docker network create --subnet 172.28.0.0/24 --opt com.docker.network.bridge.name=br-net-a trames-a
docker network create --subnet 172.29.0.0/24 --opt com.docker.network.bridge.name=br-net-b trames-b
docker network create --internal --subnet 172.30.0.0/24 trames-internal
# Ordinary Docker security profile and private IPC/cgroup/net namespaces.
docker run --ulimit nofile=256:256 -d --name net-a --network bridge \
  -p @PUBLISHED@:8080/tcp -p @PUBLISHED@:8080/udp -v /network-probe:/probe:ro \
  trames-net:probe /bin/sh -ec '/probe serve 8080 & server=$!;
  /probe dns localhost 169.254.0.53;
  /probe dns dl-cdn.alpinelinux.org 169.254.0.53;
  /probe tcp 198.18.0.1 @HTTP@;
  /probe udp 198.18.0.1 @UDP@;
  echo CONTAINER_EGRESS_PASS; wait "$server"'
for i in $(seq 1 30); do
  docker logs net-a >/tmp/net-a.log
  grep -q CONTAINER_EGRESS_PASS /tmp/net-a.log && break
  test "$(docker inspect net-a --format '{{.State.Running}}')" = true
  sleep 1
done
cat /tmp/net-a.log
grep -q CONTAINER_EGRESS_PASS /tmp/net-a.log
echo DOCKER_EGRESS_PASS
pid_a=$(docker inspect net-a --format '{{.State.Pid}}')
ip_a=$(docker inspect net-a --format '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}')
/network-probe tcp "$ip_a" 8080
/network-probe tcp 127.0.0.1 @PUBLISHED@
/network-probe udp 127.0.0.1 @PUBLISHED@
echo HOST_PUBLISHED_READY
echo DOCKER_PUBLISHED_PASS
# Add a live endpoint, then test the embedded resolver in the actual sandbox.
docker network connect --ip 172.28.0.2 --alias service trames-a net-a
/network-probe ns "$pid_a" dns service 127.0.0.11 172.28.0.2
docker run --ulimit nofile=256:256 -d --name net-b --network trames-a --ip 172.28.0.3 \
  --network-alias peer -v /network-probe:/probe:ro trames-net:probe /probe serve 8080
pid_b=$(docker inspect net-b --format '{{.State.Pid}}')
/network-probe ns "$pid_a" dns peer 127.0.0.11 172.28.0.3
/network-probe ns "$pid_b" dns service 127.0.0.11 172.28.0.2
/network-probe ns "$pid_a" tcp 172.28.0.3 8080
/network-probe ns "$pid_b" udp 172.28.0.2 8080
/network-probe ns "$pid_b" tcp 172.28.0.1 @PUBLISHED@
echo DOCKER_EMBEDDED_DNS_HAIRPIN_PASS
iptables -I DOCKER-USER 1 -i br-net-a -p udp -j DROP
/network-probe ns "$pid_b" blocked 198.18.0.1 @UDP@
iptables -D DOCKER-USER -i br-net-a -p udp -j DROP
/network-probe ns "$pid_b" udp 198.18.0.1 @UDP@
echo DOCKER_USER_FILTER_PASS
docker network disconnect trames-a net-b
docker network connect --ip 172.29.0.2 trames-b net-b
/network-probe ns "$pid_a" blocked 172.29.0.2 8080
/network-probe ns "$pid_b" blocked 172.28.0.2 8080
echo DOCKER_BRIDGE_ISOLATION_PASS
docker network disconnect trames-b net-b
docker network connect --ip 172.30.0.2 trames-internal net-b
/network-probe ns "$pid_b" blocked 198.18.0.1 @UDP@
echo DOCKER_INTERNAL_PASS
docker rm -f net-b net-a
/network-probe rebind @PUBLISHED@
docker network rm trames-a trames-b trames-internal
docker image rm trames-net:probe
if grep -q 'Failed to delete conntrack\|Failed delete conntrack' /tmp/dockerd.log; then exit 92; fi
# Reopen persisted Docker network/IPAM state within the same guest kernel.
kill "$daemon"; wait "$daemon"; daemon=
cp /tmp/dockerd.log /tmp/dockerd-before-restart.log
dockerd --containerd=/run/containerd/containerd.sock --storage-driver=vfs \
  --iptables=true --ip6tables=true --bridge=docker0 --ip-forward=true --ip-masq=true \
  --add-runtime crun=/usr/bin/crun --default-runtime=crun >/tmp/dockerd.log 2>&1 &
daemon=$!
ready /tmp/dockerd.log 'API listen on' "$daemon"
docker network inspect bridge >/tmp/restarted-bridge.json
docker network create --subnet 172.28.0.0/24 --opt com.docker.network.bridge.name=br-net-a trames-a
docker network rm trames-a
/network-probe rebind @PUBLISHED@
echo DOCKER_NETWORK_RESTART_PASS
echo DOCKER_NETWORK_CLEANUP_PASS
