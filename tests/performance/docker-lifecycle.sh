# SPDX-License-Identifier: GPL-3.0-or-later
# Manual performance probe, using an already installed original Docker/image.
set -eu
mount -t tmpfs -o mode=755 tmpfs /run
mkdir -p /dev/shm
mount -t tmpfs -o size=64m,mode=1777 tmpfs /dev/shm
mark() { echo "TRAMES_DOCKER_PROFILE_$1"; }
daemon=
cleanup() {
  if test -n "$daemon"; then
    kill "$daemon" 2>/dev/null || :
    wait "$daemon" 2>/dev/null || :
  fi
  cat /tmp/profile-dockerd.log
  dmesg
}
trap cleanup EXIT
mark DAEMON_BEGIN
dockerd >/tmp/profile-dockerd.log 2>&1 &
daemon=$!
while ! grep -q 'API listen on' /tmp/profile-dockerd.log; do
  kill -0 "$daemon"
  sleep 1
done
mark DAEMON_END
docker image inspect "$PROFILE_IMAGE" >/dev/null
mark CREATE_BEGIN
docker run -d --name profile-app "$PROFILE_IMAGE" /bin/sh -ec '
  echo profile-ready
  while test ! -e /profile-finish; do sleep 1; done'
mark CREATE_END
mark EXEC_BEGIN
docker exec profile-app /bin/sh -ec 'echo profile-exec-ok; touch /profile-finish'
mark EXEC_END
docker wait profile-app
docker rm profile-app
mark CLEANUP_END
