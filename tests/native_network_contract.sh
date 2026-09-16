#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
# The python server binds and listens before spawning the client, so the
# startup race that used to need `sleep 0.1` (and still lost on slow macOS
# runners) is gone; the client's exit code propagates through the server.
python3 "$root/tests/native_server.py" http 19991 "$root/build/bin/lumora" "$root/tests/native_network_contract.lua"
python3 "$root/tests/native_server.py" websocket 19992 "$root/build/bin/lumora" "$root/tests/native_network_contract.lua"
