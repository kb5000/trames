set -eu
mark() { printf '\nTRAMES_TTY_%s\n' "$1"; }
daemon=
cleanup() {
  test -z "$daemon" || { kill "$daemon" 2>/dev/null || :; wait "$daemon" 2>/dev/null || :; }
  mark LOG_BEGIN
  cat /tmp/tty-dockerd.log
  dmesg
  mark LOG_END
}
trap cleanup EXIT
mark DAEMON_BEGIN
dockerd --debug >/tmp/tty-dockerd.log 2>&1 &
daemon=$!
while ! grep -q 'API listen on' /tmp/tty-dockerd.log; do
  kill -0 "$daemon"
  sleep 1
done
mark DAEMON_READY
docker image inspect default-probe --format 'IMAGE={{.Id}}' || exit
printf 'GUEST_CPUS='; grep -c '^processor' /proc/cpuinfo
mark RUN_BEGIN
docker run --rm -it default-probe sh
mark RUN_END
mark COMPLETE
