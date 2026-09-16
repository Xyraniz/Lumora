#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
python3 "$root/tests/executor_request_server.py" &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
sleep 0.1
"$root/build/bin/lumora" "$root/tests/executor_request_contract.lua"
wait "$pid"
