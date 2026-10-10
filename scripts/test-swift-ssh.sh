#!/usr/bin/env bash
# Real OpenSSH peer. macOS CI uses its standard sudo-capable runner for PTY.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture="$PWD/target/ssh-fixture"
mkdir -p "$fixture"
chmod 755 "$fixture"
if [[ ! -f "$fixture/host" ]]; then ssh-keygen -q -t ed25519 -N '' -f "$fixture/host"; fi
if [[ ! -f "$fixture/client" ]]; then ssh-keygen -q -t ed25519 -N '' -f "$fixture/client"; fi
ssh_test_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
ssh_test_user=$(id -un)
cat > "$fixture/sshd.conf" <<CFG
Port $ssh_test_port
ListenAddress 127.0.0.1
HostKey $fixture/host
PidFile $fixture/daemon.pid
AuthorizedKeysFile $fixture/client.pub
StrictModes no
UsePAM no
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin no
AllowUsers $ssh_test_user
Subsystem sftp internal-sftp
CFG
sudo -n /usr/sbin/sshd -D -e -f "$fixture/sshd.conf" > "$fixture/server.log" 2>&1 &
ssh_test_pid=$!
trap 'sudo -n kill "$ssh_test_pid" 2>/dev/null || true' EXIT
python3 - "$ssh_test_port" <<'PY'
import socket,sys,time
for attempt in range(50):
    try:
        with socket.create_connection(('127.0.0.1',int(sys.argv[1])),timeout=.2): break
    except OSError: time.sleep(.1)
else: raise SystemExit('OpenSSH test server did not start')
PY
export TRAMES_SSH_TEST_PORT="$ssh_test_port" TRAMES_SSH_TEST_USER="$ssh_test_user"
export TRAMES_SSH_TEST_KEY="$fixture/client" TRAMES_SSH_TEST_HOST_KEY="$fixture/host.pub" TRAMES_SSH_TEST_ROOT="$fixture"
export TRAMES_RUST_LIB_DIR="$PWD/target/release" DYLD_LIBRARY_PATH="$PWD/target/release"
swift test --package-path apps/ios/TramesKit --disable-automatic-resolution
