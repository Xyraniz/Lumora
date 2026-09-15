#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
"$root/build/bin/lumora" "$root/tests/native_http_server.lua" & pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
sleep 0.15
python3 "$root/tests/native_http_client.py"
wait "$pid"
