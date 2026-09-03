#!/usr/bin/env bash
set -euo pipefail
VM="$1"
TMP=$(mktemp --suffix=.lua)
trap 'rm -f "$TMP"' EXIT
printf 'print("flags", arg[1])\n' > "$TMP"
JSON=$("$VM" --json --no-roblox "$TMP" value)
grep -Fq '"ok":true' <<<"$JSON"
grep -Fq 'flags' <<<"$JSON" && grep -Fq 'value' <<<"$JSON"
printf 'while true do end\n' > "$TMP"
set +e
TIMEOUT_JSON=$("$VM" --json --timeout 0.1 "$TMP")
RC=$?
set -e
test "$RC" -eq 1
grep -Fq '"ok":false' <<<"$TIMEOUT_JSON"
grep -Fq 'execution timeout' <<<"$TIMEOUT_JSON"
