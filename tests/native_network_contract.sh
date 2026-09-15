#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
 p2=""
python3 "$root/tests/native_server.py" http 19991 & p1=$!
trap 'kill "$p1" "$p2" 2>/dev/null || true' EXIT
sleep 0.1
"$root/build/bin/lumora" "$root/tests/native_network_contract.lua" http
wait "$p1"
python3 "$root/tests/native_server.py" websocket 19992 & p2=$!
sleep 0.1
"$root/build/bin/lumora" "$root/tests/native_network_contract.lua" websocket
wait "$p2"
