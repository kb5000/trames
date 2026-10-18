set -eu
mount -t tmpfs -o mode=755 tmpfs /run
mkdir -p /dev/shm
mount -t tmpfs -o size=64m,mode=1777 tmpfs /dev/shm
mark() { printf 'PROFILE_%s ' "$1"; date +%s; }
mark DAEMON_BEGIN
dockerd >/tmp/profile-dockerd.log 2>&1 &
daemon=$!
cleanup() {
  kill "$daemon" 2>/dev/null || :
  wait "$daemon" 2>/dev/null || :
  cat /tmp/profile-dockerd.log
  dmesg
}
trap cleanup EXIT
while ! grep -q 'API listen on' /tmp/profile-dockerd.log; do
  kill -0 "$daemon"
  sleep 1
done
mark DAEMON_READY
docker image inspect default-probe >/dev/null
mark CREATE_BEGIN
docker run -d --name profile-app default-probe /bin/sh -ec 'echo profile-ready; while test ! -e /finish; do sleep 1; done'
mark CREATE_END
mark EXEC_BEGIN
docker exec profile-app /bin/sh -ec 'echo profile-exec-ok; touch /finish'
mark EXEC_END
docker wait profile-app
docker rm profile-app
mark CLEANUP_END
