#!/usr/bin/env bash
set -euo pipefail
VM="$1"
TMP=$(mktemp --suffix=.lua)
trap 'rm -f "$TMP"' EXIT
printf 'error("boom")\n' > "$TMP"
set +e
JSON=$("$VM" --json --no-roblox "$TMP")
RC=$?
set -e
test "$RC" -eq 1
grep -Fq '"ok":false' <<<"$JSON"
grep -Fq 'boom' <<<"$JSON"
"$VM" --version | grep -Fxq 'lumora 0.1.0'
"$VM" --help | grep -Fq 'usage: lumora'
set +e
"$VM" --definitely-invalid "$TMP" >/dev/null 2>&1
RC=$?
set -e
test "$RC" -eq 2
