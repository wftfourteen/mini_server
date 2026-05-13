#!/usr/bin/env bash
set -u

cd "$(dirname "$0")/.."

./server >/tmp/mini_server.log 2>&1 &
server_pid=$!

cleanup() {
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT

sleep 1
python3 scripts/stress_test.py "$@"
