#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

export MINI_DB_HOST="${MINI_DB_HOST:-127.0.0.1}"
export MINI_DB_PORT="${MINI_DB_PORT:-3306}"
export MINI_DB_USER="${MINI_DB_USER:-mini_user}"
export MINI_DB_PASSWORD="${MINI_DB_PASSWORD:-mini_password}"
export MINI_DB_NAME="${MINI_DB_NAME:-mini_server}"

make test
make smoke-auth
python3 scripts/smoke_timeout.py
bash scripts/run_stress_local.sh --host 127.0.0.1 --port 8080 --clients 64 --duration 5 --keepalive 4 --report reports/stress_64c.json
