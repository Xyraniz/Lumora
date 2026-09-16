#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
# The Python server binds port 19994 and then spawns the Lumora client itself,
# so there is no startup race (on macOS a connection refused mid-connect makes
# libcurl spin until the full timeout rather than failing fast).
python3 "$root/tests/executor_request_server.py" \
    "$root/build/bin/lumora" "$root/tests/executor_request_contract.lua"
