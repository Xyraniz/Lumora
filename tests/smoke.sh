#!/usr/bin/env bash
set -euo pipefail
VM="$1"
TMP=$(mktemp --suffix=.lua)
trap 'rm -f "$TMP"' EXIT
cat > "$TMP" <<'LUA'
assert(type(arg[0]) == "string")
assert(arg[1] == "alpha")
print("lumora", arg[1])
LUA
OUT=$("$VM" "$TMP" alpha)
test "$OUT" = $'lumora\talpha'
