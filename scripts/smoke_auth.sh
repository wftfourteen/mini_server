#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
rm -f logs/access.log

export MINI_DB_HOST="${MINI_DB_HOST:-127.0.0.1}"
export MINI_DB_PORT="${MINI_DB_PORT:-3306}"
export MINI_DB_USER="${MINI_DB_USER:-mini_user}"
export MINI_DB_PASSWORD="${MINI_DB_PASSWORD:-mini_password}"
export MINI_DB_NAME="${MINI_DB_NAME:-mini_server}"

./server >/tmp/mini_server_auth.log 2>&1 &
server_pid=$!

cleanup() {
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT

sleep 1

username="alice_$(date +%s%N)"

register_code=$(curl -s -o /tmp/register.out -w "%{http_code}" \
    -X POST http://127.0.0.1:8080/register \
    -d "username=${username}&password=secret")
login_code=$(curl -s -o /tmp/login.out -w "%{http_code}" \
    -X POST http://127.0.0.1:8080/login \
    -d "username=${username}&password=secret")
bad_login_code=$(curl -s -o /tmp/bad_login.out -w "%{http_code}" \
    -X POST http://127.0.0.1:8080/login \
    -d "username=${username}&password=wrong")
duplicate_code=$(curl -s -o /tmp/duplicate.out -w "%{http_code}" \
    -X POST http://127.0.0.1:8080/register \
    -d "username=${username}&password=secret")

test "$register_code" = "201"
test "$login_code" = "200"
test "$bad_login_code" = "401"
test "$duplicate_code" = "409"
test -s logs/access.log
grep -q "POST /register 201" logs/access.log
grep -q "POST /login 200" logs/access.log
grep -q "POST /login 401" logs/access.log
grep -q "POST /register 409" logs/access.log

echo "auth smoke test passed"
