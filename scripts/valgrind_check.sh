#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

export MINI_DB_HOST="${MINI_DB_HOST:-127.0.0.1}"
export MINI_DB_PORT="${MINI_DB_PORT:-3306}"
export MINI_DB_USER="${MINI_DB_USER:-mini_user}"
export MINI_DB_PASSWORD="${MINI_DB_PASSWORD:-mini_password}"
export MINI_DB_NAME="${MINI_DB_NAME:-mini_server}"
export MINI_IDLE_TIMEOUT_SECONDS=1

mkdir -p reports
rm -f logs/access.log reports/valgrind.log

valgrind \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --error-exitcode=99 \
  --log-file=reports/valgrind.log \
  ./server >/tmp/mini_server_valgrind.log 2>&1 &
server_pid=$!

cleanup() {
    kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT

sleep 3

for _ in $(seq 1 30); do
    if curl -fsS http://127.0.0.1:8080/ >/dev/null 2>&1; then
        break
    fi
    sleep 1
done

curl -fsS http://127.0.0.1:8080/ >/dev/null
curl -fsS http://127.0.0.1:8080/test.html >/dev/null

username="vg_$(date +%s%N)"
curl -fsS -X POST http://127.0.0.1:8080/register \
    -d "username=${username}&password=secret" >/dev/null
curl -fsS -X POST http://127.0.0.1:8080/login \
    -d "username=${username}&password=secret" >/dev/null
bad_login_code=$(curl -s -o /tmp/valgrind_bad_login.out -w "%{http_code}" \
    -X POST http://127.0.0.1:8080/login \
    -d "username=${username}&password=wrong")
test "$bad_login_code" = "401"

python3 - <<'PY'
import socket
import time

sock = socket.create_connection(("127.0.0.1", 8080), timeout=3)
time.sleep(3)
sock.close()
PY

cleanup
trap - EXIT

if ! grep -q "All heap blocks were freed -- no leaks are possible" reports/valgrind.log; then
    grep -q "definitely lost: 0 bytes in 0 blocks" reports/valgrind.log
    grep -q "indirectly lost: 0 bytes in 0 blocks" reports/valgrind.log
    grep -q "possibly lost: 0 bytes in 0 blocks" reports/valgrind.log
fi

echo "valgrind leak check passed"
